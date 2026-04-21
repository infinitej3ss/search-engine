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

TEST_CASE("ISRAnd emits docs containing both terms", "[isr][and]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"cat", "dog"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"cat"}));
    b.addDocument(make_page("https://ex.com/c", {"t"}, {"dog"}));
    b.addDocument(make_page("https://ex.com/d", {"t"}, {"cat", "fish", "dog"}));
  });

  std::vector<std::unique_ptr<ISR>> kids;
  kids.push_back(word(built.idx.get(), "cat"));
  kids.push_back(word(built.idx.get(), "dog"));
  ISRAnd andIsr(built.idx.get(), std::move(kids));

  auto docs = walk_docs(andIsr);
  std::sort(docs.begin(), docs.end());
  REQUIRE(docs == std::vector<int>{0, 3});
}

TEST_CASE("ISRAnd returns empty when a term is missing", "[isr][and]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"cat"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"dog"}));
  });

  std::vector<std::unique_ptr<ISR>> kids;
  kids.push_back(word(built.idx.get(), "cat"));
  kids.push_back(word(built.idx.get(), "quokka"));  // absent
  ISRAnd andIsr(built.idx.get(), std::move(kids));

  REQUIRE_FALSE(andIsr.IsValid());
}

TEST_CASE("ISRAnd skip across sparse overlap", "[isr][and]") {
  // cat appears in every doc, rare only in doc 4. AND should skip over
  // the docs where rare is missing without getting stuck
  auto built = build_and_mmap([](IndexBuilder& b) {
    for (int i = 0; i < 10; i++) {
      std::vector<std::string> body{"cat"};
      if (i == 4) body.push_back("rare");
      b.addDocument(make_page("https://ex.com/" + std::to_string(i), {"t"}, body));
    }
  });

  std::vector<std::unique_ptr<ISR>> kids;
  kids.push_back(word(built.idx.get(), "cat"));
  kids.push_back(word(built.idx.get(), "rare"));
  ISRAnd andIsr(built.idx.get(), std::move(kids));

  auto docs = walk_docs(andIsr);
  REQUIRE(docs == std::vector<int>{4});
}

TEST_CASE("ISRAnd three-way intersection", "[isr][and]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"red", "green", "blue"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"red", "green"}));
    b.addDocument(make_page("https://ex.com/c", {"t"}, {"red", "blue"}));
    b.addDocument(make_page("https://ex.com/d", {"t"}, {"red", "green", "blue", "yellow"}));
  });

  std::vector<std::unique_ptr<ISR>> kids;
  kids.push_back(word(built.idx.get(), "red"));
  kids.push_back(word(built.idx.get(), "green"));
  kids.push_back(word(built.idx.get(), "blue"));
  ISRAnd andIsr(built.idx.get(), std::move(kids));

  auto docs = walk_docs(andIsr);
  std::sort(docs.begin(), docs.end());
  REQUIRE(docs == std::vector<int>{0, 3});
}
