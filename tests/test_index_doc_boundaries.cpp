#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "constraint_solver.h"
#include "index.h"
#include "index_builder.h"
#include "isr.h"
#include "page_data.h"
#include "test_helpers.h"

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
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"alpha", "beta", "gamma"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"delta", "epsilon"}));
    b.addDocument(make_page("https://ex.com/c", {"t"}, {"zeta"}));
  });

  for (int i = 1; i < built.idx->GetDocumentCount(); i++) {
    int prev_start, prev_end, cur_start, cur_end;
    REQUIRE(built.idx->GetDocumentRange(i - 1, prev_start, prev_end));
    REQUIRE(built.idx->GetDocumentRange(i, cur_start, cur_end));
    INFO("doc " << i - 1 << " end=" << prev_end
                << " doc " << i << " start=" << cur_start);
    REQUIRE(cur_start > prev_end);
  }
}

TEST_CASE("first word of later doc is attributed to the correct doc", "[isr][boundaries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"alpha", "beta"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"gamma", "delta"}));
    b.addDocument(make_page("https://ex.com/c", {"t"}, {"epsilon"}));
  });

  ISR gamma(built.idx.get(), "gamma");
  REQUIRE(gamma.IsValid());
  REQUIRE(gamma.GetCurrentDocId() == 1);

  ISR epsilon(built.idx.get(), "epsilon");
  REQUIRE(epsilon.IsValid());
  REQUIRE(epsilon.GetCurrentDocId() == 2);

  ISR alpha(built.idx.get(), "alpha");
  REQUIRE(alpha.IsValid());
  REQUIRE(alpha.GetCurrentDocId() == 0);
}

TEST_CASE("repeated-term ISR visits each doc exactly once", "[isr][boundaries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"common", "a"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"common", "b"}));
    b.addDocument(make_page("https://ex.com/c", {"t"}, {"common", "c"}));
  });

  ISR isr(built.idx.get(), "common");
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
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"x", "y"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"x", "y"}));
    b.addDocument(make_page("https://ex.com/c", {"t"}, {"z"}));
  });

  ConstraintSolver solver(built.idx.get());
  auto result = solver.FindAndQuery({"x", "y"});
  std::sort(result.begin(), result.end());
  REQUIRE(result == std::vector<int>{0, 1});
}

TEST_CASE("SkipToDoc lands precisely on the requested doc", "[isr][boundaries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    for (int i = 0; i < 5; i++) {
      b.addDocument(make_page("https://ex.com/" + std::to_string(i),
                              {"t"}, {"common", "w" + std::to_string(i)}));
    }
  });

  for (int target = 0; target < 5; target++) {
    ISR isr(built.idx.get(), "common");
    REQUIRE(isr.SkipToDoc(target));
    REQUIRE(isr.GetCurrentDocId() == target);
  }
}
