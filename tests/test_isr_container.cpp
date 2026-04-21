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

TEST_CASE("ISRContainer contained only equals AND", "[isr][container]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"cat", "dog"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"cat"}));
    b.addDocument(make_page("https://ex.com/c", {"t"}, {"cat", "dog", "fish"}));
  });

  std::vector<std::unique_ptr<ISR>> ins;
  ins.push_back(word(built.idx.get(), "cat"));
  ins.push_back(word(built.idx.get(), "dog"));
  std::vector<std::unique_ptr<ISR>> outs;
  ISRContainer c(built.idx.get(), std::move(ins), std::move(outs));

  auto docs = walk_docs(c);
  std::sort(docs.begin(), docs.end());
  REQUIRE(docs == std::vector<int>{0, 2});
}

TEST_CASE("ISRContainer excludes docs containing excluded term",
          "[isr][container]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"cat", "dog"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"cat", "dog", "fish"}));
    b.addDocument(make_page("https://ex.com/c", {"t"}, {"cat", "dog"}));
  });

  std::vector<std::unique_ptr<ISR>> ins;
  ins.push_back(word(built.idx.get(), "cat"));
  ins.push_back(word(built.idx.get(), "dog"));
  std::vector<std::unique_ptr<ISR>> outs;
  outs.push_back(word(built.idx.get(), "fish"));
  ISRContainer c(built.idx.get(), std::move(ins), std::move(outs));

  auto docs = walk_docs(c);
  std::sort(docs.begin(), docs.end());
  REQUIRE(docs == std::vector<int>{0, 2});  // doc 1 excluded
}

TEST_CASE("ISRContainer with an absent excluded term is a no-op",
          "[isr][container]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"cat", "dog"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"cat", "dog"}));
  });

  std::vector<std::unique_ptr<ISR>> ins;
  ins.push_back(word(built.idx.get(), "cat"));
  ins.push_back(word(built.idx.get(), "dog"));
  std::vector<std::unique_ptr<ISR>> outs;
  outs.push_back(word(built.idx.get(), "quokka"));  // absent
  ISRContainer c(built.idx.get(), std::move(ins), std::move(outs));

  auto docs = walk_docs(c);
  std::sort(docs.begin(), docs.end());
  REQUIRE(docs == std::vector<int>{0, 1});
}

TEST_CASE("ISRContainer with multiple excluded terms",
          "[isr][container]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"cat", "dog"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"cat", "dog", "fish"}));
    b.addDocument(make_page("https://ex.com/c", {"t"}, {"cat", "dog", "bird"}));
    b.addDocument(make_page("https://ex.com/d", {"t"}, {"cat", "dog"}));
  });

  std::vector<std::unique_ptr<ISR>> ins;
  ins.push_back(word(built.idx.get(), "cat"));
  ins.push_back(word(built.idx.get(), "dog"));
  std::vector<std::unique_ptr<ISR>> outs;
  outs.push_back(word(built.idx.get(), "fish"));
  outs.push_back(word(built.idx.get(), "bird"));
  ISRContainer c(built.idx.get(), std::move(ins), std::move(outs));

  auto docs = walk_docs(c);
  std::sort(docs.begin(), docs.end());
  REQUIRE(docs == std::vector<int>{0, 3});
}
