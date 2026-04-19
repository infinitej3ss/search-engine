#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "constraint_solver.h"
#include "index.h"
#include "isr.h"
#include "page_data.h"

namespace {

PageData make_page(const std::string& url,
                   const std::vector<std::string>& title,
                   const std::vector<std::string>& body) {
  PageData p;
  p.url = url;
  p.titlewords = title;
  p.words = body;
  p.anchor_text = {};
  p.distance_from_seedlist = 0;
  return p;
}

}  // namespace

TEST_CASE("doc start/end positions don't overlap across docs", "[index][boundaries]") {
  Index idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"alpha", "beta", "gamma"}));
  idx.addDocument(make_page("https://ex.com/b", {"t"}, {"delta", "epsilon"}));
  idx.addDocument(make_page("https://ex.com/c", {"t"}, {"zeta"}));
  idx.Finalize();

  for (int i = 1; i < idx.GetDocumentCount(); i++) {
    auto prev = idx.GetDocumentMetadata(i - 1);
    auto cur = idx.GetDocumentMetadata(i);
    INFO("doc " << i - 1 << " end=" << prev.end_position
                << " doc " << i << " start=" << cur.start_position);
    REQUIRE(cur.start_position > prev.end_position);
  }
}

TEST_CASE("first word of later doc is attributed to the correct doc", "[isr][boundaries]") {
  // each of these unique terms appears exactly once. the ISR for each
  // should report the doc that actually contains it, not the adjacent doc
  Index idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"alpha", "beta"}));
  idx.addDocument(make_page("https://ex.com/b", {"t"}, {"gamma", "delta"}));
  idx.addDocument(make_page("https://ex.com/c", {"t"}, {"epsilon"}));
  idx.Finalize();

  // gamma is doc 1's first body word — the old regime's vulnerable slot.
  ISR gamma(&idx, "gamma");
  REQUIRE(gamma.IsValid());
  REQUIRE(gamma.GetCurrentDocId() == 1);

  // epsilon is doc 2's only body word.
  ISR epsilon(&idx, "epsilon");
  REQUIRE(epsilon.IsValid());
  REQUIRE(epsilon.GetCurrentDocId() == 2);

  // alpha (doc 0's first) — baseline.
  ISR alpha(&idx, "alpha");
  REQUIRE(alpha.IsValid());
  REQUIRE(alpha.GetCurrentDocId() == 0);
}

TEST_CASE("repeated-term ISR visits each doc exactly once", "[isr][boundaries]") {
  // "common" appears in all three docs. walking the ISR should yield
  // docIds {0, 1, 2} with no duplicates and no misses
  Index idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"common", "a"}));
  idx.addDocument(make_page("https://ex.com/b", {"t"}, {"common", "b"}));
  idx.addDocument(make_page("https://ex.com/c", {"t"}, {"common", "c"}));
  idx.Finalize();

  ISR isr(&idx, "common");
  std::vector<int> visited;
  while (isr.IsValid()) {
    int d = isr.GetCurrentDocId();
    if (d < 0) break;
    if (visited.empty() || visited.back() != d) visited.push_back(d);
    if (!isr.Next()) break;
  }
  REQUIRE(visited == std::vector<int>{0, 1, 2});
}

TEST_CASE("AND query across adjacent docs resolves cleanly", "[constraint_solver][boundaries]") {
  Index idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"x", "y"}));
  idx.addDocument(make_page("https://ex.com/b", {"t"}, {"x", "y"}));
  idx.addDocument(make_page("https://ex.com/c", {"t"}, {"z"}));
  idx.Finalize();

  ConstraintSolver solver(&idx);
  auto result = solver.FindAndQuery({"x", "y"});
  std::sort(result.begin(), result.end());
  REQUIRE(result == std::vector<int>{0, 1});
}

TEST_CASE("SkipToDoc lands precisely on the requested doc", "[isr][boundaries]") {
  // if start positions were ambiguous, SkipToDoc(K) could land on K-1
  Index idx;
  for (int i = 0; i < 5; i++) {
    idx.addDocument(make_page("https://ex.com/" + std::to_string(i),
                              {"t"}, {"common", "w" + std::to_string(i)}));
  }
  idx.Finalize();

  for (int target = 0; target < 5; target++) {
    ISR isr(&idx, "common");
    REQUIRE(isr.SkipToDoc(target));
    REQUIRE(isr.GetCurrentDocId() == target);
  }
}
