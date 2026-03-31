#include <string>
#include <unordered_set>
#include <algorithm>

#include "url_parser.hpp"

// four tiers: top ranked, common commercial, bottom ranked, everything else
const std::unordered_set<std::string> TOP_RANKED_TLDS = {"gov", "edu"};
const std::unordered_set<std::string> MIDDLE_RANKED_TLDS = {"com", "org", "net"};
const std::unordered_set<std::string> BOTTOM_RANKED_TLDS = {"ru", "xyz"};

struct RankerInput {
  // T1
  std::string url;

  // T2
  size_t word_count;
  float_t content_to_html_ratio;

  // T3
  bool is_https;
  size_t pages_per_domain;
  size_t hop_distance;
};

// tld type
// url len
// path depth
// subdomain depth
// ip address in url
// special char density
// blacklist in url !!! special
class T1Ranker {
private:
  ParsedUrl url;

  float_t tld_rank() {
    float_t rank = 0.4; // default

    if (TOP_RANKED_TLDS.contains(url.tld)) {
      rank = 1.0;
    }
    else if (MIDDLE_RANKED_TLDS.contains(url.tld)) {
      rank = 0.7;
    }
    else if (BOTTOM_RANKED_TLDS.contains(url.tld)) {
      rank = 0.1;
    }

    return rank;
  }

  float_t url_len_rank() {
    size_t len = url.len;
    return 1.0 / (1.0 + len / 80.0);
  }

  float_t path_depth_rank() {
    return 1.0 / (1.0 + url.path_depth);
  }

  float_t subdomain_depth_rank() {
    return 1.0 / (1.0 + std::max(0.0, url.subdomain_depth - 1.0));
  }

  float_t ip_address_in_url_rank() {
    return static_cast<float_t>(url.ip_in_url);
  }

  float_t special_char_density_rank() {
    float_t density = url.special_char_density;
    return 1.0 - density;
  }

public:
  T1Ranker(ParsedUrl parsed_url) {
    url = parsed_url;
  }
    
  float_t rank() {
    // TODO for now everything is equally weighted, but we may want to weight later
    float_t sum = tld_rank() + url_len_rank() +
      path_depth_rank() + subdomain_depth_rank() +
      ip_address_in_url_rank() + special_char_density_rank();

    // TODO hardcoding for now, probably a better way to do this
    return sum / 6.0;
  }
};

class T2Ranker {
private:

public:
  T2Ranker() {
  }

  float_t rank() {
    // TODO
    return 1.0;
  }
};

class T3Ranker {
private:

public:
  T3Ranker() {
  }

  float_t rank() {
    // TODO
    return 1.0;
  }
};

class Ranker {
private:
  std::string url;
  ParsedUrl parsed_url;

public:
  Ranker(std::string url) {
    url = url;
    UrlParser parser(url);
    parsed_url = parser.parse();
  }

  float_t rank() {
    T1Ranker t1(parsed_url);
    return t1.rank();
  }
};
