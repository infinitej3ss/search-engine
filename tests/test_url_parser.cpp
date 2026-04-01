#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <string>

#include "ranker/url_parser.hpp"

TEST_CASE("extract_tld", "[url_parser]") {
  auto [url, expected] = GENERATE(table<std::string, std::string>({
    {"https://example.com/path", "com"},
    {"https://umich.edu", "edu"},
    {"http://site.gov/page", "gov"},
    {"https://example.co.uk/page", "uk"},
    {"https://localhost/test", ""},
    {"https://example.com.", "com"},
    {"https://192.168.1.1/admin", ""},
    {"", ""},
  }));

  CAPTURE(url);
  UrlParser parser(url);
  ParsedUrl parsed = parser.parse();
  REQUIRE(parsed.tld == expected);
}

TEST_CASE("get_path_depth", "[url_parser]") {
  auto [url, expected] = GENERATE(table<std::string, size_t>({
    {"https://example.com", 0},
    {"https://example.com/", 0},
    {"https://example.com/a", 1},
    {"https://example.com/a/b/c", 3},
    {"https://example.com/a/b/", 2},
    {"https://example.com/a/b?q=1", 2},
    {"https://example.com/a/b/c#frag", 3},
    {"", 0},
  }));

  CAPTURE(url);
  UrlParser parser(url);
  ParsedUrl parsed = parser.parse();
  REQUIRE(parsed.path_depth == expected);
}

TEST_CASE("get_subdomain_depth", "[url_parser]") {
  auto [url, expected] = GENERATE(table<std::string, size_t>({
    {"https://example.com", 1},
    {"https://www.example.com", 1},
    {"https://www.blog.example.com", 2},
    {"https://a.b.c.example.com", 4},
    {"https://192.168.1.1", 0},
    {"https://localhost", 0},
    {"", 0},
  }));

  CAPTURE(url);
  UrlParser parser(url);
  ParsedUrl parsed = parser.parse();
  REQUIRE(parsed.subdomain_depth == expected);
}

// note: 999.999.999.999 and 1.2.3 will return true, but it's not a valid url anyway so we don't care
TEST_CASE("is_ip_in_url", "[url_parser]") {
  auto [url, expected] = GENERATE(table<std::string, bool>({
    {"https://192.168.1.1/path", true},
    {"http://127.0.0.1:8080/page", true},
    {"https://0.0.0.0", true},
    {"https://255.255.255.255", true},
    {"https://example.com", false},
  }));

  CAPTURE(url);
  UrlParser parser(url);
  ParsedUrl parsed = parser.parse();
  REQUIRE(parsed.ip_in_url == expected);
}

TEST_CASE("special_char_density", "[url_parser]") {
  SECTION("no special chars") {
    UrlParser parser("https:example.com");
    ParsedUrl parsed = parser.parse();
    REQUIRE(parsed.special_char_density == 0.0);
  }

  SECTION("empty url") {
    UrlParser parser("");
    ParsedUrl parsed = parser.parse();
    REQUIRE(parsed.special_char_density == 0.0);
  }

  SECTION("has special chars") {
    UrlParser parser("https://example.com/path?q=1&x=2");
    ParsedUrl parsed = parser.parse();
    REQUIRE(parsed.special_char_density > 0.0);
    REQUIRE(parsed.special_char_density < 1.0);
  }
}

TEST_CASE("full parse integration", "[url_parser]") {
  UrlParser parser("https://blog.example.com/news/2026/article?id=42#top");
  ParsedUrl parsed = parser.parse();

  REQUIRE(parsed.tld == "com");
  REQUIRE(parsed.path_depth == 3);
  REQUIRE(parsed.subdomain_depth == 2);
  REQUIRE(parsed.ip_in_url == false);
  REQUIRE(parsed.special_char_density > 0.0);
  REQUIRE(parsed.blacklist_in_url == false);
  REQUIRE(parsed.len > 0);
}
