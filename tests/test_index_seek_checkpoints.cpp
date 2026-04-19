#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "index.h"
#include "isr.h"
#include "page_data.h"

namespace {

// must match the stride chosen in src/index/index.cpp
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
  // one doc per iteration, each contains the term "foo" once. after
  // N docs, the "foo" posting list has N word-posts plus N EOD posts
  Index idx;
  const int N = CHECKPOINT_STRIDE * 4 + 10;  // well past several checkpoints.
  for (int i = 0; i < N; i++) {
    idx.addDocument(make_page("https://ex.com/" + std::to_string(i), {"foo"}));
  }
  idx.Finalize();

  auto* pl = idx.getPostingList("foo");
  REQUIRE(pl != nullptr);
  REQUIRE(pl->posts.size() >= static_cast<size_t>(N));  // words + EODs
  // at least N/STRIDE checkpoints have fired. slack of 1 for off-by-one
  const size_t expected_min_checkpoints =
      (pl->posts.size() / CHECKPOINT_STRIDE) - 1;
  REQUIRE(pl->seek_absolutes.size() >= expected_min_checkpoints);
  REQUIRE(pl->seek_absolutes.size() == pl->seek_indices.size());
}

TEST_CASE("short posting lists don't need checkpoints", "[index][checkpoints]") {
  // rare terms with only a handful of posts should not force checkpoints;
  // the stride is for speeding up large-list seeks
  Index idx;
  idx.addDocument(make_page("https://ex.com/a", {"rare"}));
  idx.Finalize();

  auto* pl = idx.getPostingList("rare");
  REQUIRE(pl != nullptr);
  REQUIRE(pl->posts.size() < static_cast<size_t>(CHECKPOINT_STRIDE));
  REQUIRE(pl->seek_absolutes.size() <= 1);
}

TEST_CASE("checkpoints don't break Seek correctness", "[isr][checkpoints]") {
  // build a corpus where "foo" appears in every doc, then seek to the
  // middle and verify the ISR lands on the right doc. without functioning
  // checkpoints, this still works (falls back to linear scan); with them,
  // the seek is fast AND correct
  Index idx;
  const int N = CHECKPOINT_STRIDE * 3;
  for (int i = 0; i < N; i++) {
    idx.addDocument(make_page("https://ex.com/" + std::to_string(i), {"foo"}));
  }
  idx.Finalize();

  // seek to mid-corpus by docId. land on the requested doc
  const int target_doc = N / 2;
  ISR isr(&idx, "foo");
  REQUIRE(isr.SkipToDoc(target_doc));
  REQUIRE(isr.GetCurrentDocId() == target_doc);

  // walk forward one doc, should land on target_doc + 1.
  REQUIRE(isr.SkipToDoc(target_doc + 1));
  REQUIRE(isr.GetCurrentDocId() == target_doc + 1);
}

TEST_CASE("Seek to various positions returns correct doc", "[isr][checkpoints]") {
  // exercise several seek targets, including ones that require the binary
  // search in findCheckpoint to land on different checkpoints
  Index idx;
  const int N = CHECKPOINT_STRIDE * 5;
  for (int i = 0; i < N; i++) {
    idx.addDocument(make_page("https://ex.com/" + std::to_string(i), {"foo"}));
  }
  idx.Finalize();

  for (int target : {0, CHECKPOINT_STRIDE - 1, CHECKPOINT_STRIDE,
                     CHECKPOINT_STRIDE * 2, CHECKPOINT_STRIDE * 3 + 5,
                     N - 1}) {
    ISR isr(&idx, "foo");
    REQUIRE(isr.SkipToDoc(target));
    REQUIRE(isr.GetCurrentDocId() == target);
  }
}

TEST_CASE("checkpoints survive blob round-trip", "[index][checkpoints][blob]") {
  Index idx;
  const int N = CHECKPOINT_STRIDE * 3;
  for (int i = 0; i < N; i++) {
    idx.addDocument(make_page("https://ex.com/" + std::to_string(i), {"foo"}));
  }
  idx.Finalize();

  const std::string path = "test_checkpoints_roundtrip.blob";
  REQUIRE(idx.WriteBlob(path));

  Index reloaded;
  REQUIRE(reloaded.LoadBlob(path));

  auto* pl = reloaded.getPostingList("foo");
  REQUIRE(pl != nullptr);
  REQUIRE(pl->seek_absolutes.size() >= 2);

  ISR isr(&reloaded, "foo");
  REQUIRE(isr.SkipToDoc(N / 2));
  REQUIRE(isr.GetCurrentDocId() == N / 2);

  std::remove(path.c_str());
}
