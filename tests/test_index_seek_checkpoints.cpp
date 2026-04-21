#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "index.h"
#include "index_builder.h"
#include "isr.h"
#include "page_data.h"
#include "test_helpers.h"

// phase 2C asserts that posting lists accumulate seek checkpoints during
// build (every CHECKPOINT_STRIDE posts) and that ISR::Seek correctness is
// preserved with checkpoints in place

namespace {

// must match the stride chosen in src/index/index_builder.cpp
// if you change the constant, update this too
constexpr int CHECKPOINT_STRIDE = 128;

PageData make_page(const std::string& url,
                   const std::vector<std::string>& body) {
  PageData p;
  p.url = url;
  p.titlewords = {"t"};
  p.words = body;
  p.anchor_text = {};
  p.distance_from_seedlist = 0;
  return p;
}

}  // namespace

TEST_CASE("long posting lists accumulate seek checkpoints", "[index][checkpoints]") {
  IndexBuilder idx;
  const int N = CHECKPOINT_STRIDE * 4 + 10;
  for (int i = 0; i < N; i++) {
    idx.addDocument(make_page("https://ex.com/" + std::to_string(i), {"foo"}));
  }
  idx.Finalize();

  auto* pl = idx.getPostingList("foo");
  REQUIRE(pl != nullptr);
  REQUIRE(pl->posts.size() >= static_cast<size_t>(N));  // words + EODs
  const size_t expected_min_checkpoints =
      (pl->posts.size() / CHECKPOINT_STRIDE) - 1;
  REQUIRE(pl->seek_absolutes.size() >= expected_min_checkpoints);
  REQUIRE(pl->seek_absolutes.size() == pl->seek_indices.size());
}

TEST_CASE("short posting lists don't need checkpoints", "[index][checkpoints]") {
  IndexBuilder idx;
  idx.addDocument(make_page("https://ex.com/a", {"rare"}));
  idx.Finalize();

  auto* pl = idx.getPostingList("rare");
  REQUIRE(pl != nullptr);
  REQUIRE(pl->posts.size() < static_cast<size_t>(CHECKPOINT_STRIDE));
  REQUIRE(pl->seek_absolutes.size() <= 1);
}

TEST_CASE("checkpoints don't break Seek correctness", "[isr][checkpoints]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    const int N = CHECKPOINT_STRIDE * 3;
    for (int i = 0; i < N; i++) {
      b.addDocument(make_page("https://ex.com/" + std::to_string(i), {"foo"}));
    }
  });

  const int target_doc = (CHECKPOINT_STRIDE * 3) / 2;
  ISRWord isr(built.idx.get(), "foo");
  REQUIRE(isr.SkipToDoc(target_doc));
  REQUIRE(isr.GetCurrentDocId() == target_doc);

  REQUIRE(isr.SkipToDoc(target_doc + 1));
  REQUIRE(isr.GetCurrentDocId() == target_doc + 1);
}

TEST_CASE("Seek to various positions returns correct doc", "[isr][checkpoints]") {
  const int N = CHECKPOINT_STRIDE * 5;
  auto built = build_and_mmap([N](IndexBuilder& b) {
    for (int i = 0; i < N; i++) {
      b.addDocument(make_page("https://ex.com/" + std::to_string(i), {"foo"}));
    }
  });

  for (int target : {0, CHECKPOINT_STRIDE - 1, CHECKPOINT_STRIDE,
                     CHECKPOINT_STRIDE * 2, CHECKPOINT_STRIDE * 3 + 5,
                     N - 1}) {
    ISRWord isr(built.idx.get(), "foo");
    REQUIRE(isr.SkipToDoc(target));
    REQUIRE(isr.GetCurrentDocId() == target);
  }
}

TEST_CASE("checkpoints survive blob round-trip", "[index][checkpoints][blob]") {
  const int N = CHECKPOINT_STRIDE * 3;
  auto built = build_and_mmap([N](IndexBuilder& b) {
    for (int i = 0; i < N; i++) {
      b.addDocument(make_page("https://ex.com/" + std::to_string(i), {"foo"}));
    }
  });

  // after the round-trip, the posting list view should still expose the
  // checkpoints we built (seek_absolutes non-empty for a long posting list)
  auto v = built.idx->getPostingList("foo");
  REQUIRE(v.is_valid);
  REQUIRE(v.seek_absolutes.size() >= 2);

  ISRWord isr(built.idx.get(), "foo");
  REQUIRE(isr.SkipToDoc(N / 2));
  REQUIRE(isr.GetCurrentDocId() == N / 2);
}
