#include <catch2/catch_test_macros.hpp>

#include "ranker/dynamic/span_finder.hpp"

// each test names what the document looks like and what we expect to see.
// positions_per_term[i] = where the i-th query term appears in the document.

TEST_CASE("two adjacent terms in order are an exact phrase", "[span_finder]") {
  // query "python tutorial", document has them at positions 5 and 6
  auto result = find_span({{5}, {6}});
  REQUIRE(result.all_terms_present);
  REQUIRE(result.shortest_span == 1);
  REQUIRE(result.in_order);
  REQUIRE(result.exact_phrase);
}

TEST_CASE("two terms with a word between them are not an exact phrase", "[span_finder]") {
  // "python xyz tutorial" — terms at 5 and 7
  auto result = find_span({{5}, {7}});
  REQUIRE(result.all_terms_present);
  REQUIRE(result.shortest_span == 2);
  REQUIRE(result.in_order);
  REQUIRE_FALSE(result.exact_phrase);
}

TEST_CASE("terms in reverse order are not in query order", "[span_finder]") {
  // query "python tutorial" but document says "tutorial ... python"
  auto result = find_span({{7}, {5}});
  REQUIRE(result.all_terms_present);
  REQUIRE(result.shortest_span == 2);
  REQUIRE_FALSE(result.in_order);
  REQUIRE_FALSE(result.exact_phrase);
}

TEST_CASE("span finder picks the closest pair when terms repeat", "[span_finder]") {
  // first term at positions 2 and 10, second term at 3 and 15.
  // the closest pair is (2, 3) with distance 1.
  auto result = find_span({{2, 10}, {3, 15}});
  REQUIRE(result.all_terms_present);
  REQUIRE(result.shortest_span == 1);
  REQUIRE(result.in_order);
  REQUIRE(result.exact_phrase);
}

TEST_CASE("a missing query term means not all present", "[span_finder]") {
  // second term never appears in the document
  auto result = find_span({{5}, {}});
  REQUIRE_FALSE(result.all_terms_present);
  REQUIRE_FALSE(result.exact_phrase);
}

TEST_CASE("a single term query is trivially an exact phrase", "[span_finder]") {
  auto result = find_span({{5}});
  REQUIRE(result.all_terms_present);
  REQUIRE(result.shortest_span == 0);
  REQUIRE(result.in_order);
  REQUIRE(result.exact_phrase);
}

TEST_CASE("an empty query returns not present", "[span_finder]") {
  auto result = find_span({});
  REQUIRE_FALSE(result.all_terms_present);
}

TEST_CASE("three terms back to back in order are an exact phrase", "[span_finder]") {
  // "the quick fox" sitting at positions 5, 6, 7
  auto result = find_span({{5}, {6}, {7}});
  REQUIRE(result.all_terms_present);
  REQUIRE(result.shortest_span == 2);
  REQUIRE(result.in_order);
  REQUIRE(result.exact_phrase);
}

TEST_CASE("three scattered terms find the tightest covering window", "[span_finder]") {
  // term 0 appears at 0 and 20, term 1 at 5 and 21, term 2 at 10 and 22.
  // the tightest window is (20, 21, 22) with distance 2.
  auto result = find_span({{0, 20}, {5, 21}, {10, 22}});
  REQUIRE(result.all_terms_present);
  REQUIRE(result.shortest_span == 2);
  REQUIRE(result.in_order);
  REQUIRE(result.exact_phrase);
}

TEST_CASE("three terms close together but in the wrong order", "[span_finder]") {
  // query order is term 0, 1, 2 but document has them at 7, 5, 6
  auto result = find_span({{7}, {5}, {6}});
  REQUIRE(result.all_terms_present);
  REQUIRE(result.shortest_span == 2);
  REQUIRE_FALSE(result.in_order);
  REQUIRE_FALSE(result.exact_phrase);
}

TEST_CASE("a far apart match is still found when nothing closer exists", "[span_finder]") {
  // first term only at position 0, second term only at position 100
  auto result = find_span({{0}, {100}});
  REQUIRE(result.all_terms_present);
  REQUIRE(result.shortest_span == 100);
  REQUIRE(result.in_order);
  REQUIRE_FALSE(result.exact_phrase);
}
