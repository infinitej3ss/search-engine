#include <catch2/catch_test_macros.hpp>

#include "query/query_compiler.hpp"

using query::compile;

TEST_CASE("lowercases ASCII letters", "[query]") {
  REQUIRE(compile("Python") == std::vector<std::string>{"python"});
  REQUIRE(compile("HELLO World") ==
          std::vector<std::string>{"hello", "world"});
}

TEST_CASE("splits on whitespace", "[query]") {
  REQUIRE(compile("python tutorial") ==
          std::vector<std::string>{"python", "tutorial"});
  REQUIRE(compile("x\ty\nz") == std::vector<std::string>{"x", "y", "z"});
}

TEST_CASE("splits on punctuation", "[query]") {
  REQUIRE(compile("c++") == std::vector<std::string>{"c"});
  REQUIRE(compile("scikit-learn") ==
          std::vector<std::string>{"scikit", "learn"});
  REQUIRE(compile("foo.bar/baz") ==
          std::vector<std::string>{"foo", "bar", "baz"});
}

TEST_CASE("mixed whitespace and punctuation", "[query]") {
  REQUIRE(compile("  python, C++!  tutorial ") ==
          std::vector<std::string>{"python", "c", "tutorial"});
}

TEST_CASE("strips leading and trailing separators", "[query]") {
  REQUIRE(compile("   python   ") == std::vector<std::string>{"python"});
  REQUIRE(compile("---foo---") == std::vector<std::string>{"foo"});
}

TEST_CASE("empty and all-punctuation inputs yield no terms", "[query]") {
  REQUIRE(compile("").empty());
  REQUIRE(compile("   ").empty());
  REQUIRE(compile("!!!---???").empty());
}

TEST_CASE("keeps digits inside terms", "[query]") {
  REQUIRE(compile("python3") == std::vector<std::string>{"python3"});
  REQUIRE(compile("abc123 456") ==
          std::vector<std::string>{"abc123", "456"});
}

TEST_CASE("drops stop words", "[query]") {
  REQUIRE(compile("the python tutorial") ==
          std::vector<std::string>{"python", "tutorial"});
  REQUIRE(compile("a guide to machine learning") ==
          std::vector<std::string>{"guide", "machine", "learning"});
}

TEST_CASE("falls back to raw tokens when every term is a stop word", "[query]") {
  // Stripping all tokens would give an unusable empty query, so we return
  // the un-filtered tokens instead (de-duplication still applies).
  REQUIRE(compile("to be or the a") ==
          std::vector<std::string>{"to", "be", "or", "the", "a"});
  REQUIRE(compile("the the the") == std::vector<std::string>{"the"});
}

TEST_CASE("de-duplicates terms, keeping first-occurrence order", "[query]") {
  REQUIRE(compile("python python") == std::vector<std::string>{"python"});
  REQUIRE(compile("cat dog cat bird dog") ==
          std::vector<std::string>{"cat", "dog", "bird"});
}

TEST_CASE("stop-word removal and de-dup combine cleanly", "[query]") {
  REQUIRE(compile("the python and the tutorial") ==
          std::vector<std::string>{"python", "tutorial"});
}
