#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
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

// Small 5-doc corpus with overlapping vocab so AND/OR queries produce
// several different result sets.
std::unique_ptr<Index> build_test_index() {
  auto idx = std::make_unique<Index>();
  idx->addDocument(make_page("https://example.com/cats",
      {"My", "Cat"},         {"the", "cat", "sat", "on", "the", "mat"}));
  idx->addDocument(make_page("https://example.com/dogs",
      {"A", "Dog"},          {"the", "dog", "ran", "away"}));
  idx->addDocument(make_page("https://example.com/birds",
      {"Flying", "Bird"},    {"the", "bird", "flew"}));
  idx->addDocument(make_page("https://example.com/pets",
      {"Cat", "And", "Dog"}, {"the", "cat", "and", "the", "dog"}));
  idx->addDocument(make_page("https://example.com/quickbird",
      {"Quick", "Bird"},     {"the", "quick", "bird"}));
  idx->Finalize();
  return idx;
}

std::vector<int> sorted(std::vector<int> v) {
  std::sort(v.begin(), v.end());
  return v;
}

}  // namespace

TEST_CASE("AND queries", "[constraint_solver]") {
  auto idx = build_test_index();
  ConstraintSolver solver(idx.get());

  REQUIRE(sorted(solver.FindAndQuery({"the", "cat"}))         == std::vector<int>{0, 3});
  REQUIRE(sorted(solver.FindAndQuery({"the", "bird"}))        == std::vector<int>{2, 4});
  REQUIRE(sorted(solver.FindAndQuery({"the", "dog"}))         == std::vector<int>{1, 3});
  REQUIRE(sorted(solver.FindAndQuery({"cat", "dog"}))         == std::vector<int>{3});
  REQUIRE(sorted(solver.FindAndQuery({"the", "cat", "dog"}))  == std::vector<int>{3});
  REQUIRE(solver.FindAndQuery({"the", "quokka"}).empty());
  REQUIRE(solver.FindAndQuery({}).empty());
}

TEST_CASE("OR queries", "[constraint_solver]") {
  auto idx = build_test_index();
  ConstraintSolver solver(idx.get());

  REQUIRE(sorted(solver.FindOrQuery({"cat", "bird"}))    == std::vector<int>{0, 2, 3, 4});
  REQUIRE(sorted(solver.FindOrQuery({"dog", "flew"}))    == std::vector<int>{1, 2, 3});
  REQUIRE(sorted(solver.FindOrQuery({"quokka", "cat"}))  == std::vector<int>{0, 3});
}

TEST_CASE("ISR walks every doc for a common term", "[isr]") {
  auto idx = build_test_index();
  ISR isr(idx.get(), "the");

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
  auto idx = build_test_index();
  ISR isr(idx.get(), "the");
  REQUIRE(isr.SkipToDoc(3));
  REQUIRE(isr.GetCurrentDocId() == 3);
}

TEST_CASE("ISR::SkipToDoc past end returns false", "[isr]") {
  auto idx = build_test_index();
  ISR isr(idx.get(), "the");
  REQUIRE_FALSE(isr.SkipToDoc(99));
}

TEST_CASE("ISR on missing term is invalid", "[isr]") {
  auto idx = build_test_index();
  ISR isr(idx.get(), "quokka");
  REQUIRE_FALSE(isr.IsValid());
}

TEST_CASE("blob round-trip preserves query behavior", "[index][blob]") {
  auto idx = build_test_index();
  const std::string path = "test_constraint_solver_roundtrip.blob";

  REQUIRE(idx->WriteBlob(path));

  Index reloaded;
  REQUIRE(reloaded.LoadBlob(path));
  REQUIRE(reloaded.GetDocumentCount() == idx->GetDocumentCount());

  ConstraintSolver solver(&reloaded);
  REQUIRE(sorted(solver.FindAndQuery({"the", "cat"}))    == std::vector<int>{0, 3});
  REQUIRE(sorted(solver.FindAndQuery({"cat", "dog"}))    == std::vector<int>{3});
  REQUIRE(sorted(solver.FindOrQuery({"cat", "bird"}))    == std::vector<int>{0, 2, 3, 4});

  std::remove(path.c_str());
}
