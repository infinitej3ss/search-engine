#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
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

// small 5-doc corpus shared across several test cases
BuiltIndex build_test_index() {
  return build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://example.com/cats",
        {"My", "Cat"},         {"the", "cat", "sat", "on", "the", "mat"}));
    b.addDocument(make_page("https://example.com/dogs",
        {"A", "Dog"},          {"the", "dog", "ran", "away"}));
    b.addDocument(make_page("https://example.com/birds",
        {"Flying", "Bird"},    {"the", "bird", "flew"}));
    b.addDocument(make_page("https://example.com/pets",
        {"Cat", "And", "Dog"}, {"the", "cat", "and", "the", "dog"}));
    b.addDocument(make_page("https://example.com/quickbird",
        {"Quick", "Bird"},     {"the", "quick", "bird"}));
  });
}

std::vector<int> sorted(std::vector<int> v) {
  std::sort(v.begin(), v.end());
  return v;
}

}  // namespace

TEST_CASE("AND queries", "[constraint_solver]") {
  auto built = build_test_index();
  ConstraintSolver solver(built.idx.get());

  REQUIRE(sorted(solver.FindAndQuery({"the", "cat"}))         == std::vector<int>{0, 3});
  REQUIRE(sorted(solver.FindAndQuery({"the", "bird"}))        == std::vector<int>{2, 4});
  REQUIRE(sorted(solver.FindAndQuery({"the", "dog"}))         == std::vector<int>{1, 3});
  REQUIRE(sorted(solver.FindAndQuery({"cat", "dog"}))         == std::vector<int>{3});
  REQUIRE(sorted(solver.FindAndQuery({"the", "cat", "dog"}))  == std::vector<int>{3});
  REQUIRE(solver.FindAndQuery({"the", "quokka"}).empty());
  REQUIRE(solver.FindAndQuery({}).empty());
}

TEST_CASE("OR queries", "[constraint_solver]") {
  auto built = build_test_index();
  ConstraintSolver solver(built.idx.get());

  REQUIRE(sorted(solver.FindOrQuery({"cat", "bird"}))    == std::vector<int>{0, 2, 3, 4});
  REQUIRE(sorted(solver.FindOrQuery({"dog", "flew"}))    == std::vector<int>{1, 2, 3});
  REQUIRE(sorted(solver.FindOrQuery({"quokka", "cat"}))  == std::vector<int>{0, 3});
}

TEST_CASE("ISR walks every doc for a common term", "[isr]") {
  auto built = build_test_index();
  ISR isr(built.idx.get(), "the");

  std::vector<int> docs_visited;
  while (isr.IsValid()) {
    int d = isr.GetCurrentDocId();
    if (d < 0) break;
    if (docs_visited.empty() || docs_visited.back() != d) docs_visited.push_back(d);
    if (!isr.Next()) break;
  }
  REQUIRE(sorted(docs_visited) == std::vector<int>{0, 1, 2, 3, 4});
}

TEST_CASE("ISR::SkipToDoc lands on the target doc", "[isr]") {
  auto built = build_test_index();
  ISR isr(built.idx.get(), "the");
  REQUIRE(isr.SkipToDoc(3));
  REQUIRE(isr.GetCurrentDocId() == 3);
}

TEST_CASE("ISR::SkipToDoc past end returns false", "[isr]") {
  auto built = build_test_index();
  ISR isr(built.idx.get(), "the");
  REQUIRE_FALSE(isr.SkipToDoc(99));
}

TEST_CASE("ISR on missing term is invalid", "[isr]") {
  auto built = build_test_index();
  ISR isr(built.idx.get(), "quokka");
  REQUIRE_FALSE(isr.IsValid());
}

TEST_CASE("blob round-trip preserves query behavior", "[index][blob]") {
  // build_and_mmap already exercises the blob round-trip, so this test
  // re-runs a few queries through a freshly constructed Index from the
  // same temp file to make sure results stay consistent
  auto built = build_test_index();

  Index reloaded;
  REQUIRE(reloaded.LoadBlob(built.path));
  REQUIRE(reloaded.GetDocumentCount() == built.idx->GetDocumentCount());

  ConstraintSolver solver(&reloaded);
  REQUIRE(sorted(solver.FindAndQuery({"the", "cat"}))    == std::vector<int>{0, 3});
  REQUIRE(sorted(solver.FindAndQuery({"cat", "dog"}))    == std::vector<int>{3});
  REQUIRE(sorted(solver.FindOrQuery({"cat", "bird"}))    == std::vector<int>{0, 2, 3, 4});
}
