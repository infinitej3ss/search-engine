#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

struct ParsedUrl {
  std::string tld;
  size_t path_depth;
  size_t subdomain_depth;
  bool ip_in_url;
  double special_char_density;
  bool blacklist_in_url;
  size_t len;
};

// TODO validate set
inline const std::unordered_set<char> SPECIAL_CHARS = {'?', '&', '=', '%', '#', '+', ';', '@', ' ', '/'};

class UrlParser {
private:
  std::string url;
  std::string_view host;
  std::string_view path;

  std::string extract_tld() {
    if (is_ip_in_url()) return "";

    if (auto pos = host.find_last_of("."); pos != std::string_view::npos) {
      return std::string(host.substr(pos + 1, std::string_view::npos));
    }
    else {
      // probably not a url, no tld
      return "";
    }
  }

  size_t get_path_depth() {
    if (path.empty()) return 0;

    size_t tentative = std::count(path.begin(), path.end(), '/');
    if (path.back() == '/') tentative--;

    return tentative;
  }

  size_t get_subdomain_depth() {
    if (is_ip_in_url()) return 0;
    
    size_t tentative = std::count(host.begin(), host.end(), '.');

    if (!tentative) return 0; // probably not valid, no tld, but hopefully something else catches it? or we could give a big number
    
    // www. discount
    if (auto pos = host.find("www."); pos == 0) tentative--;

    return tentative;
  }

  bool is_ip_in_url() {
    // TODO ipv6? is it even worth it?
    // this impl is technically not totally correct but it does catch common cases
    if (host.empty()) return false;
    
    return host.find_first_not_of("0123456789.") == std::string_view::npos;
  }

  double get_special_char_density() {
    if (url.empty()) return 0.0;

    size_t special_char_count = 0;

    // TODO maybe consider doing an ascii check, and if it's not ascii then downgrading?
    // or maybe this is already covered by punycode?
    for (char c : url) {
      if (SPECIAL_CHARS.contains(c)) {
        special_char_count++;
      }
    }

    return static_cast<double>(special_char_count) / static_cast<double>(url.length());
  }

  // TODO
  bool is_blacklist_in_url() {
    return false;
  }

  // decompose sets host, path
  void decompose() {
    std::string_view view = url;

    // strip scheme
    if (auto pos = view.find("://"); pos != std::string_view::npos) {
      view.remove_prefix(pos + 3); 
    }

    // strip userinfo
    if (auto pos = view.find("@"); pos != std::string_view::npos) {
      view.remove_prefix(pos + 1); 
    }

    // split authority from path at first /
    if (auto pos = view.find('/'); pos != std::string_view::npos) {
      host = view.substr(0, pos);
      path = view.substr(pos);
    } else {
      host = view;
      path = "";
    }

    // strip port (:digits suffix on authority) -> host
    if (auto pos = host.find(':'); pos != std::string_view::npos) {
      host = host.substr(0, pos); 
    }

    // truncate path at ? or #
    if (auto pos = path.find_first_of("?#"); pos != std::string_view::npos) {
      path.remove_suffix(path.size() - pos);
    }

    // remove trailing '.' from host
    if (host.back() == '.') {
      host.remove_suffix(1);
    }

    // TODO might need more robust logic for edge cases
  }

public:
  UrlParser(std::string_view url_in) : url(url_in) {}

  ParsedUrl parse() {
    decompose();

    return ParsedUrl{
      .tld = extract_tld(),
      .path_depth = get_path_depth(),
      .subdomain_depth = get_subdomain_depth(),
      .ip_in_url = is_ip_in_url(),
      .special_char_density = get_special_char_density(),
      .blacklist_in_url = is_blacklist_in_url(),
      .len = url.length()
    };
  }
};


