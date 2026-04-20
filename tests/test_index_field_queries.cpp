#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "index.h"
#include "index_builder.h"
#include "page_data.h"
#include "test_helpers.h"

// per-doc per-term field signals read from a mmap-backed Index:
// GetBodyLength, GetFieldTermFrequency, GetFieldPositions.
// decorations: 'b' body, '@' title, '#' url, '$' anchor

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

TEST_CASE("GetBodyLength returns raw body word count", "[index][field_queries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"one", "two", "three"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"alpha"}));
    b.addDocument(make_page("https://ex.com/c", {"t"}, {}));
  });

  REQUIRE(built.idx->GetBodyLength(0) == 3);
  REQUIRE(built.idx->GetBodyLength(1) == 1);
  REQUIRE(built.idx->GetBodyLength(2) == 0);
}

TEST_CASE("GetBodyLength handles out-of-range doc_id", "[index][field_queries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"one"}));
  });

  REQUIRE(built.idx->GetBodyLength(-1) == 0);
  REQUIRE(built.idx->GetBodyLength(1) == 0);
  REQUIRE(built.idx->GetBodyLength(99) == 0);
}

TEST_CASE("GetFieldTermFrequency counts body occurrences", "[index][field_queries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"foo", "bar", "foo", "baz", "foo"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"foo"}));
  });

  REQUIRE(built.idx->GetFieldTermFrequency(0, "foo", 'b') == 3);
  REQUIRE(built.idx->GetFieldTermFrequency(0, "bar", 'b') == 1);
  REQUIRE(built.idx->GetFieldTermFrequency(0, "missing", 'b') == 0);
  REQUIRE(built.idx->GetFieldTermFrequency(1, "foo", 'b') == 1);
  REQUIRE(built.idx->GetFieldTermFrequency(1, "bar", 'b') == 0);
}

TEST_CASE("GetFieldTermFrequency counts title occurrences separately from body",
          "[index][field_queries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a",
                            {"python", "tutorial"},
                            {"learn", "python", "python"}));
  });

  REQUIRE(built.idx->GetFieldTermFrequency(0, "python", '@') == 1);
  REQUIRE(built.idx->GetFieldTermFrequency(0, "python", 'b') == 2);
  REQUIRE(built.idx->GetFieldTermFrequency(0, "tutorial", '@') == 1);
  REQUIRE(built.idx->GetFieldTermFrequency(0, "tutorial", 'b') == 0);
}

TEST_CASE("GetFieldTermFrequency counts url occurrences", "[index][field_queries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://python.org/tutorial", {"t"}, {"b"}));
  });

  REQUIRE(built.idx->GetFieldTermFrequency(0, "python", '#') == 1);
  REQUIRE(built.idx->GetFieldTermFrequency(0, "tutorial", '#') == 1);
  REQUIRE(built.idx->GetFieldTermFrequency(0, "https", '#') == 1);
  REQUIRE(built.idx->GetFieldTermFrequency(0, "missing", '#') == 0);
}

TEST_CASE("GetFieldTermFrequency counts anchor occurrences", "[index][field_queries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"b"}, {"click", "here", "click"}));
  });

  REQUIRE(built.idx->GetFieldTermFrequency(0, "click", '$') == 2);
  REQUIRE(built.idx->GetFieldTermFrequency(0, "here", '$') == 1);
  REQUIRE(built.idx->GetFieldTermFrequency(0, "click", 'b') == 0);
}

TEST_CASE("GetFieldTermFrequency handles missing term", "[index][field_queries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"b"}));
  });

  REQUIRE(built.idx->GetFieldTermFrequency(0, "nonexistent", 'b') == 0);
  REQUIRE(built.idx->GetFieldTermFrequency(0, "nonexistent", '@') == 0);
}

TEST_CASE("GetFieldTermFrequency handles out-of-range doc_id", "[index][field_queries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"foo"}));
  });

  REQUIRE(built.idx->GetFieldTermFrequency(-1, "foo", 'b') == 0);
  REQUIRE(built.idx->GetFieldTermFrequency(99, "foo", 'b') == 0);
}

TEST_CASE("GetFieldTermFrequency doesn't leak across doc boundaries",
          "[index][field_queries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"foo", "foo", "foo"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"foo"}));
    b.addDocument(make_page("https://ex.com/c", {"t"}, {"bar"}));
  });

  REQUIRE(built.idx->GetFieldTermFrequency(0, "foo", 'b') == 3);
  REQUIRE(built.idx->GetFieldTermFrequency(1, "foo", 'b') == 1);
  REQUIRE(built.idx->GetFieldTermFrequency(2, "foo", 'b') == 0);
}

TEST_CASE("GetFieldPositions returns positions in order", "[index][field_queries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"a", "foo", "b", "foo", "c", "foo"}));
  });

  auto positions = built.idx->GetFieldPositions(0, "foo", 'b');
  REQUIRE(positions.size() == 3);
  REQUIRE(std::is_sorted(positions.begin(), positions.end()));
  REQUIRE(positions[0] < positions[1]);
  REQUIRE(positions[1] < positions[2]);
}

TEST_CASE("GetFieldPositions filters by decoration", "[index][field_queries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a",
                            {"foo"},
                            {"x", "foo", "y", "foo"}));
  });

  auto body_positions = built.idx->GetFieldPositions(0, "foo", 'b');
  auto title_positions = built.idx->GetFieldPositions(0, "foo", '@');

  REQUIRE(body_positions.size() == 2);
  REQUIRE(title_positions.size() == 1);
  REQUIRE(title_positions[0] < body_positions[0]);
}

TEST_CASE("GetFieldPositions returns empty for missing term", "[index][field_queries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"foo"}));
  });

  REQUIRE(built.idx->GetFieldPositions(0, "nonexistent", 'b').empty());
  REQUIRE(built.idx->GetFieldPositions(0, "foo", '$').empty());
}

TEST_CASE("GetFieldPositions handles out-of-range doc_id", "[index][field_queries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"foo"}));
  });

  REQUIRE(built.idx->GetFieldPositions(-1, "foo", 'b').empty());
  REQUIRE(built.idx->GetFieldPositions(99, "foo", 'b').empty());
}

TEST_CASE("GetFieldPositions doesn't leak across doc boundaries",
          "[index][field_queries]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://ex.com/a", {"t"}, {"foo", "foo"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"foo"}));
  });

  auto doc0 = built.idx->GetFieldPositions(0, "foo", 'b');
  auto doc1 = built.idx->GetFieldPositions(1, "foo", 'b');

  REQUIRE(doc0.size() == 2);
  REQUIRE(doc1.size() == 1);
  REQUIRE(doc1[0] > doc0[1]);
}

TEST_CASE("field queries work correctly after blob round-trip",
          "[index][field_queries][blob]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    b.addDocument(make_page("https://python.org/a",
                            {"Python", "Tutorial"},
                            {"learn", "python", "here", "python"}));
    b.addDocument(make_page("https://ex.com/b", {"t"}, {"other"}));
  });

  Index reloaded;
  REQUIRE(reloaded.LoadBlob(built.path));

  REQUIRE(reloaded.GetBodyLength(0) == 4);
  REQUIRE(reloaded.GetFieldTermFrequency(0, "python", 'b') == 2);
  REQUIRE(reloaded.GetFieldTermFrequency(0, "python", '@') == 1);
  REQUIRE(reloaded.GetFieldPositions(0, "python", 'b').size() == 2);
}

TEST_CASE("field queries scale across checkpoint boundaries",
          "[index][field_queries][checkpoints]") {
  auto built = build_and_mmap([](IndexBuilder& b) {
    const int N = 500;
    for (int i = 0; i < N; i++) {
      b.addDocument(make_page("https://ex.com/" + std::to_string(i),
                              {"t"}, {"common", "common"}));
    }
  });

  for (int doc : {0, 1, 100, 250, 499}) {
    REQUIRE(built.idx->GetFieldTermFrequency(doc, "common", 'b') == 2);
    REQUIRE(built.idx->GetFieldPositions(doc, "common", 'b').size() == 2);
  }
}
