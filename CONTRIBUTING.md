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

the main executable is `build/engine`. the test executable is `build/tests`.

### running tests

```bash
ctest --test-dir build --output-on-failure
```

