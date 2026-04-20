#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "constraint_solver.h"
#include "index.h"
#include "index_builder.h"
#include "page_data.h"
#include "test_helpers.h"

// tokens are case-folded on insert so queries (which the query compiler
// always lowercases) match documents whose parser emitted mixed-case
// tokens. only one posting list per term, regardless of source case

namespace {

PageData make_page(const std::string& url,
                   const std::vector<std::string>& title,
                   const std::vector<std::string>& body,
                   const std::vector<std::string>& anchor = {}) {
  PageData p;
  p.url = url;
  p.titlewords = title;
  p.words = body;
  p.anchor_text = anchor;
  p.distance_from_seedlist = 0;
  return p;
}

}  // namespace

TEST_CASE("mixed-case title tokens match lowercase query", "[index][case_folding]") {
  IndexBuilder idx;
  idx.addDocument(make_page("https://py.example/", {"The", "Python", "Tutorial"}, {"learn", "python"}));
  idx.Finalize();

  REQUIRE(idx.GetDocumentFrequency("python") == 1);
  REQUIRE(idx.GetDocumentFrequency("tutorial") == 1);

  REQUIRE(idx.GetDocumentFrequency("Python") == 0);
  REQUIRE(idx.GetDocumentFrequency("Tutorial") == 0);
  REQUIRE(idx.GetDocumentFrequency("THE") == 0);
}

TEST_CASE("mixed-case body tokens match lowercase query", "[index][case_folding]") {
  IndexBuilder idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"Hello", "WORLD", "hello"}));
  idx.Finalize();

  // three occurrences ("Hello", "WORLD", "hello") should collapse to two
  // posting lists ("hello" with 2 occurrences, "world" with 1)
  auto* hello = idx.getPostingList("hello");
  REQUIRE(hello != nullptr);
  REQUIRE(hello->num_docs == 1);
  REQUIRE(hello->word_occurrences == 2);

  auto* world = idx.getPostingList("world");
  REQUIRE(world != nullptr);
  REQUIRE(world->num_docs == 1);
  REQUIRE(world->word_occurrences == 1);

  REQUIRE(idx.GetDocumentFrequency("Hello") == 0);
  REQUIRE(idx.GetDocumentFrequency("WORLD") == 0);
}

TEST_CASE("mixed-case anchor tokens match lowercase query", "[index][case_folding]") {
  IndexBuilder idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"b"}, {"Click", "HERE"}));
  idx.Finalize();

  REQUIRE(idx.GetDocumentFrequency("click") == 1);
  REQUIRE(idx.GetDocumentFrequency("here") == 1);
}

TEST_CASE("mixed-case tokens survive blob round-trip", "[index][case_folding][blob]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"Python", "TUTORIAL"},
                            {"Python", "is", "Fun"}));
  });

  REQUIRE(built.idx->GetDocumentFrequency("python") == 1);
  REQUIRE(built.idx->GetDocumentFrequency("tutorial") == 1);
  REQUIRE(built.idx->GetDocumentFrequency("fun") == 1);
  REQUIRE(built.idx->GetDocumentFrequency("Python") == 0);
}

TEST_CASE("AND query on mixed-case title returns matching doc", "[constraint_solver][case_folding]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://py.example/tutorial",
                            {"The", "Python", "Tutorial"},
                            {"an", "intro"}));
    b.addDocument(make_page("https://cpp.example/tutorial",
                            {"The", "C++", "Tutorial"},
                            {"an", "intro"}));
  });
  ConstraintSolver solver(built.idx.get());

  REQUIRE(solver.FindAndQuery({"python", "tutorial"}) == std::vector<int>{0});
  REQUIRE(solver.FindOrQuery({"python"}) == std::vector<int>{0});
}

TEST_CASE("url parts (already lowercased) are unaffected", "[index][case_folding]") {
  IndexBuilder idx;
  idx.addDocument(make_page("https://Python.org/Tutorial", {"x"}, {"y"}));
  idx.Finalize();

  REQUIRE(idx.GetDocumentFrequency("python") == 1);
  REQUIRE(idx.GetDocumentFrequency("tutorial") == 1);
  REQUIRE(idx.GetDocumentFrequency("org") == 1);
}
