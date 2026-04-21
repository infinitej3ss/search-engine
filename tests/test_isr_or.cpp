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

std::vector<int> collect_docs(ISR& isr) {
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

TEST_CASE("ISROr emits docs containing either term", "[isr][or]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"cat"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"fish"}));
    b.addDocument(make_page("https://ex.com/c", {"t"}, {"dog"}));
    b.addDocument(make_page("https://ex.com/d", {"t"}, {"cat", "dog"}));
  });

  std::vector<std::unique_ptr<ISR>> kids;
  kids.push_back(word(built.idx.get(), "cat"));
  kids.push_back(word(built.idx.get(), "dog"));
  ISROr orIsr(built.idx.get(), std::move(kids));

  auto docs = collect_docs(orIsr);
  std::sort(docs.begin(), docs.end());
  REQUIRE(docs == std::vector<int>{0, 2, 3});
}

TEST_CASE("ISROr with a missing term yields the other term's docs",
          "[isr][or]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"cat"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"cat"}));
  });

  std::vector<std::unique_ptr<ISR>> kids;
  kids.push_back(word(built.idx.get(), "cat"));
  kids.push_back(word(built.idx.get(), "quokka"));
  ISROr orIsr(built.idx.get(), std::move(kids));

  auto docs = collect_docs(orIsr);
  std::sort(docs.begin(), docs.end());
  REQUIRE(docs == std::vector<int>{0, 1});
}

TEST_CASE("ISROr all terms missing is invalid", "[isr][or]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"cat"}));
  });

  std::vector<std::unique_ptr<ISR>> kids;
  kids.push_back(word(built.idx.get(), "quokka"));
  kids.push_back(word(built.idx.get(), "narwhal"));
  ISROr orIsr(built.idx.get(), std::move(kids));

  REQUIRE_FALSE(orIsr.IsValid());
}
