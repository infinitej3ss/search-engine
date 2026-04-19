#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "index.h"
#include "page_data.h"

// derives per-doc per-term field signals from posting lists:
// GetBodyLength, GetFieldTermFrequency, GetFieldPositions. these replace
// the previous approach of reaching into DocumentMetadata.body_words
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
  Index idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"one", "two", "three"}));
  idx.addDocument(make_page("https://ex.com/b", {"t"}, {"alpha"}));
  idx.addDocument(make_page("https://ex.com/c", {"t"}, {}));
  idx.Finalize();

  REQUIRE(idx.GetBodyLength(0) == 3);
  REQUIRE(idx.GetBodyLength(1) == 1);
  REQUIRE(idx.GetBodyLength(2) == 0);
}

TEST_CASE("GetBodyLength handles out-of-range doc_id", "[index][field_queries]") {
  Index idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"one"}));
  idx.Finalize();

  REQUIRE(idx.GetBodyLength(-1) == 0);
  REQUIRE(idx.GetBodyLength(1) == 0);
  REQUIRE(idx.GetBodyLength(99) == 0);
}

TEST_CASE("GetFieldTermFrequency counts body occurrences", "[index][field_queries]") {
  Index idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"foo", "bar", "foo", "baz", "foo"}));
  idx.addDocument(make_page("https://ex.com/b", {"t"}, {"foo"}));
  idx.Finalize();

  REQUIRE(idx.GetFieldTermFrequency(0, "foo", 'b') == 3);
  REQUIRE(idx.GetFieldTermFrequency(0, "bar", 'b') == 1);
  REQUIRE(idx.GetFieldTermFrequency(0, "missing", 'b') == 0);
  REQUIRE(idx.GetFieldTermFrequency(1, "foo", 'b') == 1);
  REQUIRE(idx.GetFieldTermFrequency(1, "bar", 'b') == 0);
}

TEST_CASE("GetFieldTermFrequency counts title occurrences separately from body",
          "[index][field_queries]") {
  // "python" appears in both title and body. decoration filter distinguishes
  Index idx;
  idx.addDocument(make_page("https://ex.com/a",
                            {"python", "tutorial"},
                            {"learn", "python", "python"}));
  idx.Finalize();

  REQUIRE(idx.GetFieldTermFrequency(0, "python", '@') == 1);  // title
  REQUIRE(idx.GetFieldTermFrequency(0, "python", 'b') == 2);  // body
  REQUIRE(idx.GetFieldTermFrequency(0, "tutorial", '@') == 1);
  REQUIRE(idx.GetFieldTermFrequency(0, "tutorial", 'b') == 0);
}

TEST_CASE("GetFieldTermFrequency counts url occurrences", "[index][field_queries]") {
  Index idx;
  idx.addDocument(make_page("https://python.org/tutorial", {"t"}, {"b"}));
  idx.Finalize();

  REQUIRE(idx.GetFieldTermFrequency(0, "python", '#') == 1);
  REQUIRE(idx.GetFieldTermFrequency(0, "tutorial", '#') == 1);
  REQUIRE(idx.GetFieldTermFrequency(0, "https", '#') == 1);
  REQUIRE(idx.GetFieldTermFrequency(0, "missing", '#') == 0);
}

TEST_CASE("GetFieldTermFrequency counts anchor occurrences", "[index][field_queries]") {
  Index idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"b"}, {"click", "here", "click"}));
  idx.Finalize();

  REQUIRE(idx.GetFieldTermFrequency(0, "click", '$') == 2);
  REQUIRE(idx.GetFieldTermFrequency(0, "here", '$') == 1);
  REQUIRE(idx.GetFieldTermFrequency(0, "click", 'b') == 0);
}

TEST_CASE("GetFieldTermFrequency handles missing term", "[index][field_queries]") {
  Index idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"b"}));
  idx.Finalize();

  // term that doesn't exist in the dictionary at all
  REQUIRE(idx.GetFieldTermFrequency(0, "nonexistent", 'b') == 0);
  REQUIRE(idx.GetFieldTermFrequency(0, "nonexistent", '@') == 0);
}

TEST_CASE("GetFieldTermFrequency handles out-of-range doc_id", "[index][field_queries]") {
  Index idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"foo"}));
  idx.Finalize();

  REQUIRE(idx.GetFieldTermFrequency(-1, "foo", 'b') == 0);
  REQUIRE(idx.GetFieldTermFrequency(99, "foo", 'b') == 0);
}

TEST_CASE("GetFieldTermFrequency doesn't leak across doc boundaries",
          "[index][field_queries]") {
  // "foo" in doc 0 body must not count toward doc 1, and vice versa
  Index idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"foo", "foo", "foo"}));
  idx.addDocument(make_page("https://ex.com/b", {"t"}, {"foo"}));
  idx.addDocument(make_page("https://ex.com/c", {"t"}, {"bar"}));
  idx.Finalize();

  REQUIRE(idx.GetFieldTermFrequency(0, "foo", 'b') == 3);
  REQUIRE(idx.GetFieldTermFrequency(1, "foo", 'b') == 1);
  REQUIRE(idx.GetFieldTermFrequency(2, "foo", 'b') == 0);
}

TEST_CASE("GetFieldPositions returns positions in order", "[index][field_queries]") {
  Index idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"a", "foo", "b", "foo", "c", "foo"}));
  idx.Finalize();

  auto positions = idx.GetFieldPositions(0, "foo", 'b');
  REQUIRE(positions.size() == 3);
  REQUIRE(std::is_sorted(positions.begin(), positions.end()));
  // positions should be strictly increasing
  REQUIRE(positions[0] < positions[1]);
  REQUIRE(positions[1] < positions[2]);
}

TEST_CASE("GetFieldPositions filters by decoration", "[index][field_queries]") {
  Index idx;
  idx.addDocument(make_page("https://ex.com/a",
                            {"foo"},                 // title
                            {"x", "foo", "y", "foo"}));  // body
  idx.Finalize();

  auto body_positions = idx.GetFieldPositions(0, "foo", 'b');
  auto title_positions = idx.GetFieldPositions(0, "foo", '@');

  REQUIRE(body_positions.size() == 2);
  REQUIRE(title_positions.size() == 1);

  // title is added before body in addDocument, so title_positions[0] < all body_positions
  REQUIRE(title_positions[0] < body_positions[0]);
}

TEST_CASE("GetFieldPositions returns empty for missing term", "[index][field_queries]") {
  Index idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"foo"}));
  idx.Finalize();

  REQUIRE(idx.GetFieldPositions(0, "nonexistent", 'b').empty());
  REQUIRE(idx.GetFieldPositions(0, "foo", '$').empty());  // anchor; term is body-only
}

TEST_CASE("GetFieldPositions handles out-of-range doc_id", "[index][field_queries]") {
  Index idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"foo"}));
  idx.Finalize();

  REQUIRE(idx.GetFieldPositions(-1, "foo", 'b').empty());
  REQUIRE(idx.GetFieldPositions(99, "foo", 'b').empty());
}

TEST_CASE("GetFieldPositions doesn't leak across doc boundaries",
          "[index][field_queries]") {
  Index idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"foo", "foo"}));
  idx.addDocument(make_page("https://ex.com/b", {"t"}, {"foo"}));
  idx.Finalize();

  auto doc0 = idx.GetFieldPositions(0, "foo", 'b');
  auto doc1 = idx.GetFieldPositions(1, "foo", 'b');

  REQUIRE(doc0.size() == 2);
  REQUIRE(doc1.size() == 1);
  // doc 1's position is strictly greater than both of doc 0's positions
  REQUIRE(doc1[0] > doc0[1]);
}

TEST_CASE("field queries work correctly after blob round-trip",
          "[index][field_queries][blob]") {
  Index idx;
  idx.addDocument(make_page("https://python.org/a",
                            {"Python", "Tutorial"},
                            {"learn", "python", "here", "python"}));
  idx.addDocument(make_page("https://ex.com/b", {"t"}, {"other"}));
  idx.Finalize();

  const std::string path = "test_field_queries_roundtrip.blob";
  REQUIRE(idx.WriteBlob(path));

  Index reloaded;
  REQUIRE(reloaded.LoadBlob(path));

  REQUIRE(reloaded.GetBodyLength(0) == 4);
  REQUIRE(reloaded.GetFieldTermFrequency(0, "python", 'b') == 2);
  REQUIRE(reloaded.GetFieldTermFrequency(0, "python", '@') == 1);
  REQUIRE(reloaded.GetFieldPositions(0, "python", 'b').size() == 2);

  std::remove(path.c_str());
}

TEST_CASE("field queries scale across checkpoint boundaries",
          "[index][field_queries][checkpoints]") {
  // with many docs, the posting list for a common term crosses checkpoint
  // boundaries. each doc's field query should still return just that
  // doc's positions
  Index idx;
  const int N = 500;  // well past CHECKPOINT_STRIDE=128
  for (int i = 0; i < N; i++) {
    idx.addDocument(make_page("https://ex.com/" + std::to_string(i),
                              {"t"}, {"common", "common"}));
  }
  idx.Finalize();

  // each doc contributes 2 occurrences; check a handful spanning the corpus
  for (int doc : {0, 1, 100, 250, 499}) {
    REQUIRE(idx.GetFieldTermFrequency(doc, "common", 'b') == 2);
    REQUIRE(idx.GetFieldPositions(doc, "common", 'b').size() == 2);
  }
}
