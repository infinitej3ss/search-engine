#pragma once

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "../static/static_ranker.hpp"

// T2/T3 dynamic weight profile. One instance per query-intent class.
// inline (not constexpr) so load_and_apply_weights can overwrite at runtime.
struct WeightProfile {
  double w_metastream;  // T1
  double w_span;        // T2
  double w_quality;     // T3
};

inline WeightProfile GENERAL      = {0.35, 0.45, 0.20};
inline WeightProfile NAVIGATIONAL = {0.60, 0.15, 0.25};

// Reads "key value" lines from `path` and assigns them to T1_WEIGHTS and the
// profile globals. Blank and `#`-prefixed lines are skipped. Unknown keys and
// parse errors warn on stderr but don't throw — a typo shouldn't kill the
// engine during tuning.
// Returns false only when the file couldn't be opened.
inline bool load_and_apply_weights(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open()) return false;

  std::string line;
  while (std::getline(in, line)) {
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos || line[start] == '#') continue;

    std::istringstream iss(line);
    std::string key;
    double value;
    if (!(iss >> key >> value)) {
      std::cerr << "[weights] bad line: " << line << "\n";
      continue;
    }

    // keep order in sync with T1_SIGNALS in static_ranker.hpp
    if      (key == "tld")                   T1_WEIGHTS[0] = value;
    else if (key == "url_len")               T1_WEIGHTS[1] = value;
    else if (key == "path_depth")            T1_WEIGHTS[2] = value;
    else if (key == "subdomain_depth")       T1_WEIGHTS[3] = value;
    else if (key == "ip_in_url")             T1_WEIGHTS[4] = value;
    else if (key == "special_char_density")  T1_WEIGHTS[5] = value;
    else if (key == "blacklist")             T1_WEIGHTS[6] = value;

    else if (key == "profile.general.metastream")      GENERAL.w_metastream      = value;
    else if (key == "profile.general.span")            GENERAL.w_span            = value;
    else if (key == "profile.general.quality")         GENERAL.w_quality         = value;
    else if (key == "profile.navigational.metastream") NAVIGATIONAL.w_metastream = value;
    else if (key == "profile.navigational.span")       NAVIGATIONAL.w_span       = value;
    else if (key == "profile.navigational.quality")    NAVIGATIONAL.w_quality    = value;

    else std::cerr << "[weights] unknown key: " << key << "\n";
  }
  return true;
}
