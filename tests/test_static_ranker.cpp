#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ranker/static/static_ranker.hpp"

// smoke test, verifies headers compile and basic wiring works
TEST_CASE("t1 rank returns value in [0, 1]", "[ranker]") {
  StaticRanker ranker(RankerInput{.url = "https://example.com/path"});
  double score = ranker.rank();
  REQUIRE(score >= 0.0);
  REQUIRE(score <= 1.0);
}

TEST_CASE("gov url scores higher than xyz url", "[ranker]") {
  StaticRanker gov(RankerInput{.url = "https://example.gov/page"});
  StaticRanker xyz(RankerInput{.url = "https://example.xyz/page"});
  REQUIRE(gov.rank() > xyz.rank());
}

TEST_CASE("ip url scores lower than domain url", "[ranker]") {
  StaticRanker ip(RankerInput{.url = "http://192.168.1.1/page"});
  StaticRanker domain(RankerInput{.url = "https://example.com/page"});
  REQUIRE(ip.rank() < domain.rank());
}

TEST_CASE("shorter url scores higher than longer url", "[ranker]") {
  StaticRanker short_url(RankerInput{.url = "https://example.com"});
  StaticRanker long_url(RankerInput{.url = "https://example.com/a/very/deep/nested/path/to/some/page"});
  REQUIRE(short_url.rank() > long_url.rank());
}

TEST_CASE("weight modification changes score", "[ranker]") {
  auto saved = T1_WEIGHTS;

  StaticRanker ranker(RankerInput{.url = "https://example.gov/page"});

  // tld only
  T1_WEIGHTS = {1.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  double tld_only = ranker.rank();

  // all signals
  T1_WEIGHTS = saved;
  double all = ranker.rank();

  // gov tld scores 1.0 alone, but other signals pull it down
  REQUIRE(tld_only > all);

  T1_WEIGHTS = saved;
}
