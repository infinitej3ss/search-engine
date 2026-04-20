## contributing

### requirements

- CMake 3.20+
- Ninja
- a C++20 compiler (clang or gcc)

### building

```bash
cmake --preset default
cmake --build build
```

Produces `build/search_server`, `build/shard_server`, `build/engine`, `build/build_index`, and `build/tests`.

### Build the index from crawl data

Crawler page files go in `data/` with the naming convention `crawled_page_data_rank_<R>_num_<N>`. Then:

```bash
cd data
../build/build_index
```

This produces `index_rank_<R>.blob` files (one per rank tier). `build_index` scans for all `crawled_page_data_rank_<R>_num_*` files in the current directory and combines all `num_*` files per rank into a single blob. The search server loads the blobs automatically.

In distributed deployment, each machine runs `build_index` against its own locally-crawled files. Shards across machines have identical filenames (`index_rank_0.blob`, etc.) but different contents because the crawler sharded the raw data by domain hash.

### Run the search server (single-machine mode)

```bash
build/search_server 8080 ./frontend
```

Open `http://localhost:8080/index.html` in a browser. The server loads all `index_rank_*.blob` files from `data/` and handles `/search?q=...` queries in-process.

Environment variables:
- `LEADER_DATA_DIR` — where to find blob files (default: `data`)

### Run the standalone query engine (CLI)

```bash
build/engine data
```

Type a query at the `>` prompt. Useful for quick testing without the browser.

### Run distributed locally (multiple shards + leader)

Each shard runs its own `shard_server` process with a subset of the index blobs. The leader uses `config/shards.txt` to fan out queries across them.

**1. Set up per-shard data directories** — simplest is one blob per shard via symlinks:

```bash
for i in 0 1 2 3 4; do
  mkdir -p data/shard_$i
  ln -sf ../index_rank_$i.blob data/shard_$i/index_rank_$i.blob
done
```

**2. Start each shard on its own port:**

```bash
SHARD_DATA_DIR=data/shard_0 build/shard_server 9000 . &
SHARD_DATA_DIR=data/shard_1 build/shard_server 9001 . &
SHARD_DATA_DIR=data/shard_2 build/shard_server 9002 . &
SHARD_DATA_DIR=data/shard_3 build/shard_server 9003 . &
SHARD_DATA_DIR=data/shard_4 build/shard_server 9004 . &
```

**3. Configure `config/shards.txt`** with the shard addresses and timeouts:

```
timeout_ms 5000         # leader-side cap on waiting for all shards
shard_timeout_ms 4000   # per-shard HTTP timeout (shorter than leader's)
results_needed 20       # after merge+dedup, keep this many before returning

127.0.0.1:9000
127.0.0.1:9001
127.0.0.1:9002
127.0.0.1:9003
127.0.0.1:9004
```

**4. Start the leader pointing at the shards config:**

```bash
LEADER_SHARDS_CONFIG=config/shards.txt build/search_server 8080 ./frontend
```

The leader detects the env var and runs in distributed mode, fanning out queries to all shards in parallel, merging results, deduplicating by URL, and sorting by combined score.

**5. Shut down:**

```bash
for p in 8080 9000 9001 9002 9003 9004; do
  lsof -ti :$p | xargs kill -9 2>/dev/null
done
```

In distributed mode, each shard reads its own copy of `config/weights.txt` — keep them in sync across machines (or use a shared filesystem).

### Run distributed across multiple machines (VMs)

Same flow as local, but one shard per VM. On each shard VM:

**1. Build the index on each shard VM** — on each shard VM, clone the repo, build, and run the crawler to produce `crawled_page_data_rank_<R>_num_<N>` files in `data/`. Then run `build/build_index` from the `data/` directory to produce `index_rank_<R>.blob` files.

The crawler shards raw pages across VMs by domain hash, so each machine has a disjoint slice of the corpus. The blob filenames are identical across machines — only the contents differ.

**2. Start the shard service:**

```bash
SHARD_DATA_DIR=~/search-engine/data build/shard_server 9000 .
```

Bind port 9000 to an interface reachable by the leader. Make sure the firewall allows inbound connections from the leader's IP on that port.

**3. On the leader VM**, edit `config/shards.txt` with each shard's reachable address:

```
timeout_ms 5000
shard_timeout_ms 4000
results_needed 20

10.0.0.10:9000
10.0.0.11:9000
10.0.0.12:9000
10.0.0.13:9000
10.0.0.14:9000
```

**4. Start the leader:**

```bash
LEADER_SHARDS_CONFIG=config/shards.txt build/search_server 8080 ./frontend
```

Expose port 8080 to clients (users' browsers).

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

Add a test by dropping a `.cpp` file in `tests/`. CMake picks it up automatically.
