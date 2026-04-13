#pragma once

#include <algorithm>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

struct ParsedUrl {
  std::string tld;
  size_t path_depth;
  size_t subdomain_depth;
  bool ip_in_url;
  double special_char_density;
  bool blacklist_in_url;
  bool is_asset;
  size_t len;
};

// TODO validate set
inline const std::unordered_set<char> SPECIAL_CHARS = {'?', '&', '=', '%', '#', '+', ';', '@', ' ', '/'};

// blacklist loaded from file at startup
// file format: one term per line
// lines starting with # are comments, empty lines are ignored
// terms prefixed with * match anywhere (e.g. "*hardcore")
// terms without prefix require url boundary chars on both sides
struct Blacklist {
  std::vector<std::string> anywhere; // match as substring anywhere
  std::vector<std::string> bounded; // require boundary chars

  bool loaded = false;
};

inline Blacklist BLACKLIST;

inline void load_blacklist(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) return;

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') continue;

    // strip trailing whitespace
    while (!line.empty() && (line.back() == ' ' || line.back() == '\r' || line.back() == '\t')) {
      line.pop_back();
    }
    if (line.empty()) continue;

    // lowercase
    std::transform(line.begin(), line.end(), line.begin(),
      [](unsigned char c) { return std::tolower(c); });

    if (line[0] == '*') {
      BLACKLIST.anywhere.push_back(line.substr(1));
    } else {
      BLACKLIST.bounded.push_back(line);
    }
  }

  BLACKLIST.loaded = true;
}

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

  // spam / porn / obscenity detection in urls
  // terms loaded from blacklist file at startup via load_blacklist()
  // static ranker flags these; dynamic ranker can override when the
  // user query itself contains the term (intentional search)

  static bool is_url_boundary(char c) {
    return c == '-' || c == '.' || c == '/' || c == '?'
        || c == '=' || c == '+' || c == '_' || c == '&';
  }

  bool is_blacklist_in_url() {
    if (!BLACKLIST.loaded) return false;

    std::string lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(),
      [](unsigned char c) { return std::tolower(c); });

    // strip scheme before matching
    std::string_view check = lower;
    if (auto pos = check.find("://"); pos != std::string_view::npos) {
      check.remove_prefix(pos + 3);
    }

    for (const auto& term : BLACKLIST.anywhere) {
      if (check.find(term) != std::string_view::npos) return true;
    }

    for (const auto& term : BLACKLIST.bounded) {
      size_t pos = 0;
      while ((pos = check.find(term, pos)) != std::string_view::npos) {
        bool left_ok = (pos == 0) || is_url_boundary(check[pos - 1]);
        size_t end = pos + term.size();
        bool right_ok = (end == check.size()) || is_url_boundary(check[end]);
        if (left_ok && right_ok) return true;
        pos++;
      }
    }

    return false;
  }

  // checks the file extension at the end of the path (before query string)
  bool detect_asset() {
    if (path.empty()) return false;

    static const std::unordered_set<std::string_view> ASSET_EXTS = {
      // images
      ".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp", ".ico", ".bmp",
      // styles / scripts
      ".css", ".js",
      // fonts
      ".woff", ".woff2", ".ttf", ".eot",
      // archives
      ".zip", ".tar", ".gz", ".bz2", ".7z", ".rar",
      // media
      ".mp3", ".mp4", ".wav", ".avi", ".mkv", ".flac", ".ogg", ".webm",
      // data (not human-readable pages)
      ".json", ".xml", ".csv", ".rss", ".atom",
      // binaries
      ".exe", ".dmg", ".deb", ".rpm", ".msi", ".apk",
    };

    // TODO consider path-based exclusions:
    // /robots.txt, /sitemap.xml, /favicon.ico
    // /wp-admin/, /cgi-bin/, /wp-json/
    // /api/, /graphql
    // TODO consider subdomain-based exclusions:
    // cdn.*, static.*, assets.*, media.*

    auto dot = path.rfind('.');
    if (dot == std::string_view::npos) return false;

    std::string ext(path.substr(dot));
    std::transform(ext.begin(), ext.end(), ext.begin(),
      [](unsigned char c) { return std::tolower(c); });

    return ASSET_EXTS.contains(ext);
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
      .is_asset = detect_asset(),
      .len = url.length()
    };
  }
};
