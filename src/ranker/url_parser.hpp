#include <string>
#include <unordered_set>

// TODO optimize struct
struct ParsedUrl {
  std::string tld;
  size_t path_depth;
  size_t subdomain_depth;
  bool ip_in_url;
  float_t special_char_density;
  bool blacklist_in_url;
  size_t len;
};

// TODO validate set
const std::unordered_set<char> SPECIAL_CHARS = {'?', '&', '=', '%', '#', '+', ';', '@', ' ', '/'};

class UrlParser {
private:
  std::string url;

  // TODO
  std::string extract_tld() {
    return "foo";
  }

  // TODO
  size_t get_path_depth() {
    return 0;
  }

  // TODO
  size_t get_subdomain_depth() {
    return 0;
  }

  // TODO
  bool is_ip_in_url() {
    return 0;
  }

  float_t get_special_char_density() {
    size_t special_char_count = 0;

    // TODO maybe consider doing an ascii check, and if it's not ascii then downgrading?
    // or maybe this is already covered by punycode?
    for (char c : url) {
      if (SPECIAL_CHARS.contains(c)) {
        special_char_count++;
      }
    }

    return static_cast<float_t>(special_char_count) / static_cast<float_t>(url.length());
  }

  // TODO
  bool is_blacklist_in_url() {
    return false;
  }
public:
  UrlParser(std::string url_in) {
    url = url_in;
  }

  ParsedUrl parse() {
    ParsedUrl parsed_url{
      .tld = extract_tld(), 
      .path_depth = get_path_depth(),
      .subdomain_depth = get_subdomain_depth(),
      .ip_in_url = is_ip_in_url(),
      .special_char_density = get_special_char_density(),
      .blacklist_in_url = is_blacklist_in_url(),
      .len = url.length()
    };

    return parsed_url;
  }
};


