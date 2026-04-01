#pragma once

#include <array>
#include <string>
#include <string_view>
#include <unordered_set>
#include <algorithm>

#include "url_parser.hpp"

// four tiers: top ranked, common commercial, bottom ranked, everything else
inline const std::unordered_set<std::string> TOP_RANKED_TLDS = {"gov", "edu"};
inline const std::unordered_set<std::string> MIDDLE_RANKED_TLDS = {"com", "org", "net"};
inline const std::unordered_set<std::string> BOTTOM_RANKED_TLDS = {"ru", "xyz"};

struct RankerInput {
  // t1
  std::string url;

  // t2
  size_t word_count;
  double content_to_html_ratio;

  // t3
  bool is_https;
  size_t pages_per_domain;
  size_t hop_distance;
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

constexpr size_t T1_NUM_SIGNALS = 6;

using T1SignalFn = double(*)(const ParsedUrl&);

constexpr std::array<T1SignalFn, T1_NUM_SIGNALS> T1_SIGNALS = {
  tld_rank,
  url_len_rank,
  path_depth_rank,
  subdomain_depth_rank,
  ip_in_url_rank,
  special_char_density_rank,
};

// default weights — tune via eval
inline std::array<double, T1_NUM_SIGNALS> T1_WEIGHTS = {
  1.0, 1.0, 1.0, 1.0, 1.0, 1.0
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

// TODO t2 — content-based ranking
inline double t2_rank(const RankerInput& /* input */) {
  return 1.0;
}

// TODO t3 — domain/hop-based ranking
inline double t3_rank(const RankerInput& /* input */) {
  return 1.0;
}

class Ranker {
private:
  std::string url;
  ParsedUrl parsed_url;

public:
  Ranker(std::string_view url_in) : url(url_in) {
    UrlParser parser(url);
    parsed_url = parser.parse();
  }

  double rank() {
    return t1_rank(parsed_url);
  }
};
