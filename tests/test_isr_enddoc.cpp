#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

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

TEST_CASE("ISREndDoc walks every doc exactly once", "[isr][enddoc]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"alpha"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"beta"}));
    b.addDocument(make_page("https://ex.com/c", {"t"}, {"gamma"}));
  });

  ISREndDoc docs(built.idx.get());
  std::vector<int> visited;
  while (docs.IsValid()) {
    visited.push_back(docs.GetCurrentDocIndex());
    if (!docs.Next()) break;
  }
  REQUIRE(visited == std::vector<int>{0, 1, 2});
}

TEST_CASE("ISREndDoc Seek lands on doc containing the location",
          "[isr][enddoc]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"a", "b", "c"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"d", "e", "f"}));
    b.addDocument(make_page("https://ex.com/c", {"t"}, {"g", "h", "i"}));
  });

  // pick a position inside doc 1 and seek — should land on doc 1
  int doc1_start = 0, doc1_end = 0;
  REQUIRE(built.idx->GetDocumentRange(1, doc1_start, doc1_end));

  ISREndDoc docs(built.idx.get());
  REQUIRE(docs.Seek(doc1_start + 1));
  REQUIRE(docs.GetCurrentDocIndex() == 1);

  // seeking past the last doc returns false
  int last_end = 0, tmp = 0;
  REQUIRE(built.idx->GetDocumentRange(built.idx->GetDocumentCount() - 1, tmp, last_end));
  REQUIRE_FALSE(docs.Seek(last_end + 1000));
}

TEST_CASE("ISREndDoc GetStart/End match the doc range", "[isr][enddoc]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"one", "two"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"three"}));
  });

  ISREndDoc docs(built.idx.get());
  REQUIRE(docs.IsValid());

  int s0 = 0, e0 = 0;
  REQUIRE(built.idx->GetDocumentRange(0, s0, e0));
  REQUIRE(docs.GetStartLocation() == s0);
  REQUIRE(docs.GetEndLocation() == e0);

  REQUIRE(docs.Next());
  int s1 = 0, e1 = 0;
  REQUIRE(built.idx->GetDocumentRange(1, s1, e1));
  REQUIRE(docs.GetStartLocation() == s1);
  REQUIRE(docs.GetEndLocation() == e1);
}
