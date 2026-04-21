#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
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

std::vector<int> walk_docs(ISR& isr) {
  std::vector<int> out;
  while (isr.IsValid()) {
    int d = isr.GetCurrentDocId();
    if (d < 0) break;
    if (out.empty() || out.back() != d) out.push_back(d);
    if (!isr.Next()) break;
  }
  return out;
}

std::unique_ptr<ISR> word(const Index* idx, const std::string& t) {
  return std::make_unique<ISRWord>(idx, t);
}

}  // namespace

TEST_CASE("ISRPhrase matches exact adjacency", "[isr][phrase]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"},
                            {"python", "tutorial", "online"}));
    b.addDocument(make_page("https://ex.com/b", {"t"},
                            {"tutorial", "python", "online"}));
    b.addDocument(make_page("https://ex.com/c", {"t"},
                            {"learn", "python", "tutorial"}));
  });

  std::vector<std::unique_ptr<ISR>> kids;
  kids.push_back(word(built.idx.get(), "python"));
  kids.push_back(word(built.idx.get(), "tutorial"));
  ISRPhrase phrase(built.idx.get(), std::move(kids));

  auto docs = walk_docs(phrase);
  std::sort(docs.begin(), docs.end());
  REQUIRE(docs == std::vector<int>{0, 2});
}

TEST_CASE("ISRPhrase reverse order does not match", "[isr][phrase]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"},
                            {"tutorial", "python"}));
  });

  std::vector<std::unique_ptr<ISR>> kids;
  kids.push_back(word(built.idx.get(), "python"));
  kids.push_back(word(built.idx.get(), "tutorial"));
  ISRPhrase phrase(built.idx.get(), std::move(kids));

  REQUIRE_FALSE(phrase.IsValid());
}

TEST_CASE("ISRPhrase span width equals phrase length - 1", "[isr][phrase]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"},
                            {"one", "quick", "brown", "fox"}));
  });

  std::vector<std::unique_ptr<ISR>> kids;
  kids.push_back(word(built.idx.get(), "quick"));
  kids.push_back(word(built.idx.get(), "brown"));
  kids.push_back(word(built.idx.get(), "fox"));
  ISRPhrase phrase(built.idx.get(), std::move(kids));

  REQUIRE(phrase.IsValid());
  REQUIRE(phrase.GetEndLocation() - phrase.GetStartLocation() == 2);
}

TEST_CASE("ISRPhrase only matches within same field", "[isr][phrase]") {
  // last word of title is "python"; first word of body is "tutorial".
  // positions are consecutive but decorations differ — should not match
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"python"},
                            {"tutorial", "online"}));
    // doc 1 has the phrase legitimately in the body
    b.addDocument(make_page("https://ex.com/b", {"t"},
                            {"python", "tutorial", "online"}));
  });

  std::vector<std::unique_ptr<ISR>> kids;
  kids.push_back(word(built.idx.get(), "python"));
  kids.push_back(word(built.idx.get(), "tutorial"));
  ISRPhrase phrase(built.idx.get(), std::move(kids));

  auto docs = walk_docs(phrase);
  REQUIRE(docs == std::vector<int>{1});
}
