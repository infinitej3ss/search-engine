## Contributing

### Build

```bash
cmake --preset default
cmake --build build
```

Produces `build/engine`, `build/build_index`, and `build/tests`.

### Run the search engine

Crawler data lives in `data/`. From there:

```bash
cd data
../build/build_index      # builds index blobs from crawler files
../build/engine           # starts the query prompt
```

Type a query at the `>` prompt. Edit `weights.txt` between queries to retune — no rebuild needed.

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

Add a test by dropping a `.cpp` file in `tests/`. CMake picks it up automatically.
