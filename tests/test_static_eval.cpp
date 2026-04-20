#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ranker/static/static_ranker.hpp"

using Catch::Matchers::WithinAbs;

// path to blacklist file relative to where tests run from (build dir)
static const char* BLACKLIST_PATH = "../src/ranker/static/data/blacklist.txt";

static bool ensure_blacklist() {
  if (!BLACKLIST.loaded) load_blacklist(BLACKLIST_PATH);
  return BLACKLIST.loaded;
}

// static ranker eval suite
// each test encodes an expected ordering: "url A should score higher than url B"
// when a test fails, it means the weights produce a ranking inversion
//
// to add new eval cases: use EXPECT_BEATS(better_url, worse_url)
// keep a comment above with the rationale

inline void dump_signals(const std::string& url) {
  UrlParser parser(url);
  ParsedUrl p = parser.parse();

  const char* names[] = {
    "tld", "url_len", "path_depth", "subdomain_depth",
    "ip_in_url", "special_char_density", "blacklist"
  };

  for (size_t i = 0; i < T1_NUM_SIGNALS; i++) {
    UNSCOPED_INFO(names[i] << ": " << T1_SIGNALS[i](p) << " (w=" << T1_WEIGHTS[i] << ")");
  }

  ensure_blacklist();
  StaticRanker r(RankerInput{.url = url});
  UNSCOPED_INFO("total: " << r.rank());
}

inline double score(const std::string& url) {
  ensure_blacklist();
  StaticRanker r(RankerInput{.url = url});
  return r.rank();
}

// dumps signals for both urls on failure, asserts a > b
#define EXPECT_BEATS(a, b) do { \
  dump_signals(a); \
  dump_signals(b); \
  ensure_blacklist(); \
  StaticRanker ra(RankerInput{.url = a}); \
  StaticRanker rb(RankerInput{.url = b}); \
  REQUIRE(ra.rank() > rb.rank()); \
} while(0)

// tld authority

TEST_CASE("edu beats com, all else equal", "[eval]") {
  EXPECT_BEATS("https://umich.edu", "https://umich.com");
}

TEST_CASE("gov beats com, all else equal", "[eval]") {
  EXPECT_BEATS("https://whitehouse.gov", "https://whitehouse.com");
}

TEST_CASE("com beats spam tld", "[eval]") {
  EXPECT_BEATS("https://example.com", "https://example.top");
}

TEST_CASE("known country code beats unknown tld", "[eval]") {
  // .de is in middle tier (0.7), .pizza is default (0.4)
  EXPECT_BEATS("https://example.de", "https://example.pizza");
}

// real world: good vs bad

TEST_CASE("official python docs beat deep blog post", "[eval]") {
  EXPECT_BEATS(
    "https://docs.python.org/3/tutorial/",
    "https://some-blog.com/2024/01/15/random/python/tips/page3.html");
}

TEST_CASE("wikipedia beats deep forum thread", "[eval]") {
  EXPECT_BEATS(
    "https://en.wikipedia.org/wiki/Binary_search",
    "https://forums.example.com/t/how-to/search/binary/thread/12345?page=3&sort=new");
}

TEST_CASE("clean com beats ip address url", "[eval]") {
  EXPECT_BEATS("https://example.com/page", "http://192.168.1.1/admin/login");
}

// path depth vs tld tradeoff

TEST_CASE("deep path on good tld still beats shallow path on spam tld", "[eval]") {
  EXPECT_BEATS(
    "https://eecs.umich.edu/courses/eecs498/assignments/project3",
    "https://free-stuff.tk");
}

TEST_CASE("moderate path on com beats shallow path on spam tld", "[eval]") {
  EXPECT_BEATS(
    "https://developer.mozilla.org/en-US/docs/Web",
    "https://click-here.cyou");
}

// blacklist

TEST_CASE("clean url beats blacklisted url on same tld", "[eval]") {
  EXPECT_BEATS("https://example.com/cooking-tips", "https://example.com/hardcore-videos");
}

TEST_CASE("blacklisted term in subdomain penalizes", "[eval]") {
  EXPECT_BEATS("https://docs.example.com", "https://porn.example.com");
}

// special chars

TEST_CASE("clean url beats query-heavy url", "[eval]") {
  EXPECT_BEATS(
    "https://example.com/page",
    "https://example.com/page?id=123&ref=456&utm_source=spam&token=abc");
}

// subdomain depth

TEST_CASE("simple subdomain beats deep subdomain chain", "[eval]") {
  EXPECT_BEATS("https://blog.example.com", "https://a.b.c.d.example.com");
}

// edge cases / tradeoff tensions

TEST_CASE("slightly longer good url beats very short spam url", "[eval]") {
  // url_len should not dominate tld
  EXPECT_BEATS("https://stackoverflow.com/questions/python", "https://x.tk");
}

TEST_CASE("www subdomain does not penalize", "[eval]") {
  ensure_blacklist();
  double with_www = StaticRanker(RankerInput{.url = "https://www.example.com"}).rank();
  double without_www = StaticRanker(RankerInput{.url = "https://example.com"}).rank();
  REQUIRE_THAT(with_www, WithinAbs(without_www, 0.05));
}

// hard exclusions, assets return -1.0

TEST_CASE("images are excluded", "[eval]") {
  REQUIRE(score("https://example.com/photo.jpg") < 0.0);
  REQUIRE(score("https://example.com/icon.png") < 0.0);
  REQUIRE(score("https://example.com/logo.svg") < 0.0);
}

TEST_CASE("scripts and styles are excluded", "[eval]") {
  REQUIRE(score("https://example.com/app.js") < 0.0);
  REQUIRE(score("https://example.com/style.css") < 0.0);
}

TEST_CASE("archives and binaries are excluded", "[eval]") {
  REQUIRE(score("https://example.com/release.zip") < 0.0);
  REQUIRE(score("https://example.com/installer.exe") < 0.0);
}

TEST_CASE("media files are excluded", "[eval]") {
  REQUIRE(score("https://example.com/song.mp3") < 0.0);
  REQUIRE(score("https://example.com/video.mp4") < 0.0);
}

TEST_CASE("html pages are not excluded", "[eval]") {
  REQUIRE(score("https://example.com/page.html") > 0.0);
  REQUIRE(score("https://example.com/about") > 0.0);
  REQUIRE(score("https://example.com/") > 0.0);
}

TEST_CASE("pdf is not excluded", "[eval]") {
  // pdfs can be valid search results
  REQUIRE(score("https://example.com/paper.pdf") > 0.0);
}
