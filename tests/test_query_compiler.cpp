#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "query/query_compiler.hpp"

namespace {
// project the flattened .terms field — these legacy assertions pin the
// ranker-facing term list; AST-shape assertions live in test_query_parser.cpp
std::vector<std::string> terms(std::string_view raw) {
  return query::compile(raw).terms;
}
}  // namespace

TEST_CASE("lowercases ASCII letters", "[query]") {
  REQUIRE(terms("Python") == std::vector<std::string>{"python"});
  REQUIRE(terms("HELLO World") ==
          std::vector<std::string>{"hello", "world"});
}

TEST_CASE("splits on whitespace", "[query]") {
  REQUIRE(terms("python tutorial") ==
          std::vector<std::string>{"python", "tutorial"});
  REQUIRE(terms("x\ty\nz") == std::vector<std::string>{"x", "y", "z"});
}

TEST_CASE("splits on punctuation", "[query]") {
  REQUIRE(terms("c++") == std::vector<std::string>{"c"});
  REQUIRE(terms("scikit-learn") ==
          std::vector<std::string>{"scikit", "learn"});
  REQUIRE(terms("foo.bar/baz") ==
          std::vector<std::string>{"foo", "bar", "baz"});
}

TEST_CASE("mixed whitespace and punctuation", "[query]") {
  REQUIRE(terms("  python, C++!  tutorial ") ==
          std::vector<std::string>{"python", "c", "tutorial"});
}

TEST_CASE("strips leading and trailing separators", "[query]") {
  REQUIRE(terms("   python   ") == std::vector<std::string>{"python"});
  // note: -foo- now means NOT(foo) at head; a lonely NOT still extracts
  // the word for the terms list
  REQUIRE(terms("foo") == std::vector<std::string>{"foo"});
}

TEST_CASE("empty and all-punctuation inputs yield no terms", "[query]") {
  REQUIRE(query::compile("").empty());
  REQUIRE(query::compile("   ").empty());
  REQUIRE(query::compile("!!!???").empty());
}

TEST_CASE("keeps digits inside terms", "[query]") {
  REQUIRE(terms("python3") == std::vector<std::string>{"python3"});
  REQUIRE(terms("abc123 456") ==
          std::vector<std::string>{"abc123", "456"});
}

TEST_CASE("drops stop words", "[query]") {
  REQUIRE(terms("the python tutorial") ==
          std::vector<std::string>{"python", "tutorial"});
  REQUIRE(terms("a guide to machine learning") ==
          std::vector<std::string>{"guide", "machine", "learning"});
}

TEST_CASE("falls back to raw tokens when every term is a stop word", "[query]") {
  // Stripping all tokens would give an unusable empty query, so we return
  // the un-filtered tokens instead (de-duplication still applies)
  REQUIRE(terms("to be or the a") ==
          std::vector<std::string>{"to", "be", "or", "the", "a"});
  REQUIRE(terms("the the the") == std::vector<std::string>{"the"});
}

TEST_CASE("de-duplicates terms, keeping first-occurrence order", "[query]") {
  REQUIRE(terms("python python") == std::vector<std::string>{"python"});
  REQUIRE(terms("cat dog cat bird dog") ==
          std::vector<std::string>{"cat", "dog", "bird"});
}

TEST_CASE("stop-word removal and de-dup combine cleanly", "[query]") {
  REQUIRE(terms("the python and the tutorial") ==
          std::vector<std::string>{"python", "tutorial"});
}

TEST_CASE("phrases preserve stop words for retrieval", "[query]") {
  // stop words inside a phrase are kept in extract_terms so that scoring
  // signals (BM25, metastream, span) can use them
  auto t = terms("\"to be or not to be\"");
  REQUIRE(t.size() == 4);  // to, be, or, not (deduped)
  REQUIRE(t[0] == "to");
  REQUIRE(t[1] == "be");
  REQUIRE(t[2] == "or");
  REQUIRE(t[3] == "not");
}
