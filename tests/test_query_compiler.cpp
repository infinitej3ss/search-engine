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
  REQUIRE(compile("a\tb\nc") == std::vector<std::string>{"a", "b", "c"});
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

TEST_CASE("preserves repeated terms", "[query]") {
  REQUIRE(compile("python python") ==
          std::vector<std::string>{"python", "python"});
}
