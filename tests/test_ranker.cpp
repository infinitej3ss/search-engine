#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ranker/ranker.hpp"

using Catch::Matchers::WithinAbs;

// smoke test, verifies headers compile and basic wiring works
TEST_CASE("t1 rank returns value in [0, 1]", "[ranker]") {
  UrlParser parser("https://example.com/path");
  ParsedUrl parsed = parser.parse();
  double score = t1_rank(parsed);
  REQUIRE(score >= 0.0);
  REQUIRE(score <= 1.0);
}

TEST_CASE("ip in url is penalized", "[ranker]") {
  ParsedUrl with_ip{.ip_in_url = true};
  ParsedUrl without_ip{.ip_in_url = false};
  REQUIRE(ip_in_url_rank(with_ip) < ip_in_url_rank(without_ip));
}

TEST_CASE("tld ranking tiers", "[ranker]") {
  ParsedUrl gov{.tld = "gov"};
  ParsedUrl com{.tld = "com"};
  ParsedUrl xyz{.tld = "xyz"};
  ParsedUrl unknown{.tld = "io"};

  REQUIRE(tld_rank(gov) > tld_rank(com));
  REQUIRE(tld_rank(com) > tld_rank(unknown));
  REQUIRE(tld_rank(unknown) > tld_rank(xyz));
}

TEST_CASE("zero weight skips signal", "[ranker]") {
  auto saved = T1_WEIGHTS;

  T1_WEIGHTS = {1.0, 0.0, 0.0, 0.0, 0.0, 0.0};

  ParsedUrl gov{.tld = "gov", .len = 100};
  REQUIRE_THAT(t1_rank(gov), WithinAbs(1.0, 1e-9));

  T1_WEIGHTS = saved;
}
