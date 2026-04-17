#pragma once

#include <array>
#include <string>
#include <unordered_set>
#include <algorithm>

#include "url_parser.hpp"

// four tiers: top ranked, common commercial, bottom ranked, everything else
inline const std::unordered_set<std::string> TOP_RANKED_TLDS = {
  "gov", "edu", "mil", "int",
};

// commercial, country
inline const std::unordered_set<std::string> MIDDLE_RANKED_TLDS = {
  "com", "org", "net", "io", "dev", "co", "app", "info",
  "uk", "de", "ca", "au", "jp", "fr", "nl", "ch", "se", "no",
  "fi", "dk", "nz", "ie", "at", "be", "it", "es", "pt", "kr",
};

// spam domains
// https://blog.cloudflare.com/top-level-domains-email-phishing-threats/
// https://www.spamhaus.org/resource-hub/domain-reputation/the-most-abused-top-level-domains-in-2018/
inline const std::unordered_set<std::string> BOTTOM_RANKED_TLDS = {
  "tk", "ml", "ga", "gq", "cf",
  "uno", "sbs", "best", "beauty", "top", "hair", "monster",
  "cyou", "wiki", "makeup",
  "bar", "pw", "bit",
};

struct RankerInput {
  // t1
  std::string url;

  // t2
  bool is_https;
  size_t pages_per_domain; // probably skipping
  size_t hop_distance;

  // t3
  size_t word_count; // maybe skipping?
  double content_to_html_ratio; // probably skipping

};

// t1 signal functions — each takes a parsed url and returns a score in [0, 1]

inline double tld_rank(const ParsedUrl& url) {
  if (TOP_RANKED_TLDS.contains(url.tld)) return 1.0;
  if (MIDDLE_RANKED_TLDS.contains(url.tld)) return 0.7;
  if (BOTTOM_RANKED_TLDS.contains(url.tld)) return 0.1;
  return 0.4;
}

inline double url_len_rank(const ParsedUrl& url) {
  return 1.0 / (1.0 + url.len / 80.0);
}

inline double path_depth_rank(const ParsedUrl& url) {
  return 1.0 / (1.0 + url.path_depth);
}

inline double subdomain_depth_rank(const ParsedUrl& url) {
  return 1.0 / (1.0 + std::max(0.0, static_cast<double>(url.subdomain_depth) - 1.0));
}

inline double ip_in_url_rank(const ParsedUrl& url) {
  return url.ip_in_url ? 0.0 : 1.0;
}

inline double special_char_density_rank(const ParsedUrl& url) {
  return 1.0 - url.special_char_density;
}

inline double blacklist_rank(const ParsedUrl& url) {
  return url.blacklist_in_url ? 0.0 : 1.0;
}

constexpr size_t T1_NUM_SIGNALS = 7;

using T1SignalFn = double(*)(const ParsedUrl&);

constexpr std::array<T1SignalFn, T1_NUM_SIGNALS> T1_SIGNALS = {
  tld_rank,
  url_len_rank,
  path_depth_rank,
  subdomain_depth_rank,
  ip_in_url_rank,
  special_char_density_rank,
  blacklist_rank,
};

// weights — tune via eval (tests/test_static_eval.cpp)
// order: tld, url_len, path_depth, subdomain_depth, ip_in_url, special_char_density, blacklist
// TODO refactor into config
inline std::array<double, T1_NUM_SIGNALS> T1_WEIGHTS = {
  3.0, 1.0, 1.0, 0.5, 1.0, 2.0, 1.0
};

inline double t1_rank(const ParsedUrl& url) {
  double total = 0.0;
  double weight_sum = 0.0;

  for (size_t i = 0; i < T1_NUM_SIGNALS; i++) {
    if (T1_WEIGHTS[i] != 0.0) {
      total += T1_WEIGHTS[i] * T1_SIGNALS[i](url);
      weight_sum += T1_WEIGHTS[i];
    }
  }

  return weight_sum > 0.0 ? total / weight_sum : 0.0;
}

// TODO t2 — domain/hop-based ranking (hop based only currently)
inline double t2_rank(const RankerInput& input) {
  return 1.0 / (1 + input.hop_distance);
}

// TODO t2 — content-based ranking
inline double t3_rank(const RankerInput& /* input */) {
  return 1.0;
}

class StaticRanker {
private:
  RankerInput input;
  ParsedUrl parsed_url;

public:
  StaticRanker(const RankerInput& input_in) : input(input_in) {
    UrlParser parser(input.url);
    parsed_url = parser.parse();
  }

  // returns score in [0.0, 1.0] for normal pages
  // returns -1.0 for hard exclusions (assets, non-content)
  double rank() {
    if (parsed_url.is_asset) return -1.0;

    return t1_rank(parsed_url) * t2_rank(input);

    // TODO add t3 later
  }
};
