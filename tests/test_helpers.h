#pragma once

// small helpers used across the index test suite.
// common pattern: build an in-memory index via IndexBuilder, write a temp
// V4 blob, mmap-load it into an Index. lets us exercise the full
// build → serialize → mmap → query path in one test

#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>

#include "index.h"
#include "index_builder.h"

// owns a temp blob on disk and an open mmap-backed Index over it.
// the blob file is removed when this object is destroyed
struct BuiltIndex {
  std::string path;
  std::unique_ptr<Index> idx;

  BuiltIndex() = default;
  BuiltIndex(const BuiltIndex&) = delete;
  BuiltIndex& operator=(const BuiltIndex&) = delete;
  BuiltIndex(BuiltIndex&&) = default;
  BuiltIndex& operator=(BuiltIndex&&) = default;
  ~BuiltIndex() {
    idx.reset();
    if (!path.empty()) std::remove(path.c_str());
  }
};

inline BuiltIndex build_and_mmap(std::function<void(IndexBuilder&)> fill) {
  IndexBuilder b;
  fill(b);
  b.Finalize();

  static std::atomic<int> counter{0};
  std::string path = "test_tmp_" + std::to_string(counter.fetch_add(1)) + ".blob";
  b.WriteBlobV4(path);

  BuiltIndex result;
  result.path = path;
  result.idx = std::make_unique<Index>();
  result.idx->LoadBlob(path);
  return result;
}
