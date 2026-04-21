#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "query/query_ast.hpp"

namespace {

std::string pretty(std::string_view raw) {
  auto ast = query::parse(raw);
  if (!ast) return "<null>";
  query::normalize(*ast);
  return ast->to_string();
}

}  // namespace

TEST_CASE("bare word parses to WORD", "[parser]") {
  REQUIRE(pretty("python") == "python");
}

TEST_CASE("implicit AND flattens whitespace-joined words", "[parser]") {
  // "the" is a stop word and gets dropped by normalize; "python tutorial"
  // is two non-stop words -> AND
  REQUIRE(pretty("python tutorial") == "AND(python, tutorial)");
}

TEST_CASE("pipe is OR", "[parser]") {
  REQUIRE(pretty("python|rust") == "OR(python, rust)");
  REQUIRE(pretty("python | rust") == "OR(python, rust)");
}

TEST_CASE("AND binds tighter than OR", "[parser]") {
  REQUIRE(pretty("cat dog | fish") == "OR(AND(cat, dog), fish)");
}

TEST_CASE("parens regroup", "[parser]") {
  REQUIRE(pretty("cat (dog | fish)") == "AND(cat, OR(dog, fish))");
}

TEST_CASE("quoted phrase", "[parser]") {
  REQUIRE(pretty("\"machine learning\"") == "\"machine learning\"");
}

TEST_CASE("phrase combined with bare words", "[parser]") {
  REQUIRE(pretty("python \"machine learning\"") ==
          "AND(python, \"machine learning\")");
}

TEST_CASE("NOT with dash shorthand", "[parser]") {
  // "cat -fish" -> AND(cat, NOT(fish))
  REQUIRE(pretty("cat -fish") == "AND(cat, NOT(fish))");
}

TEST_CASE("OR chains flatten", "[parser]") {
  REQUIRE(pretty("cat | dog | fish") == "OR(cat, dog, fish)");
}

TEST_CASE("nested parens", "[parser]") {
  REQUIRE(pretty("(cat | dog) (red | blue)") ==
          "AND(OR(cat, dog), OR(red, blue))");
}

TEST_CASE("complex mix", "[parser]") {
  REQUIRE(pretty("python \"machine learning\" (rust|go) -java") ==
          "AND(python, \"machine learning\", OR(rust, go), NOT(java))");
}

TEST_CASE("tolerant: unterminated quote consumes rest", "[parser]") {
  // unterminated phrase just consumes whatever remains as phrase body
  auto ast = query::parse("\"python tutorial");
  REQUIRE(ast != nullptr);
  query::normalize(*ast);
  REQUIRE(ast->to_string() == "\"python tutorial\"");
}

TEST_CASE("tolerant: unbalanced paren auto-closes", "[parser]") {
  auto ast = query::parse("(python rust");
  REQUIRE(ast != nullptr);
  query::normalize(*ast);
  REQUIRE(ast->to_string() == "AND(python, rust)");
}

TEST_CASE("tolerant: empty input returns null", "[parser]") {
  REQUIRE(query::parse("") == nullptr);
  REQUIRE(query::parse("   ") == nullptr);
  REQUIRE(query::parse("!!!") == nullptr);
}

TEST_CASE("stop words inside phrases survive normalization", "[parser]") {
  // "the cat" → phrase keeps "the"; bare "cat the" → AND drops "the",
  // leaving a single child. single-child AND is still AND(cat) since
  // normalize doesn't collapse wrapper nodes — the ISR compiler handles it
  REQUIRE(pretty("\"the cat\"") == "\"the cat\"");
  REQUIRE(pretty("cat the") == "AND(cat)");
}

TEST_CASE("phrase of one word collapses to word", "[parser]") {
  REQUIRE(pretty("\"python\"") == "python");
}
