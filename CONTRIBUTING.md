## Contributing

### Build

```bash
cmake --preset default
cmake --build build
```

Produces `build/search_server`, `build/engine`, `build/build_index`, and `build/tests`.

### Build the index from crawl data

Crawler page files go in `data/` with the naming convention `crawled_page_rank_<R>_num_<N>`. Then:

```bash
cd data
../build/build_index
```

This produces `index_rank_<R>.blob` files (one per rank tier). The search server loads these automatically.

### Run the search server

```bash
build/search_server 8080 ./frontend
```

Open `http://localhost:8080/index.html` in a browser. The server serves the frontend as static files and handles `/search?q=...` queries via the search plugin.

### Run the standalone query engine (CLI)

```bash
cd data
../build/engine
```

Type a query at the `>` prompt. Useful for quick testing without the browser.

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

Add a test by dropping a `.cpp` file in `tests/`. CMake picks it up automatically.
