#pragma once

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "../src/ranker/static/static_ranker.hpp"

// dynamic weight profile. one instance per query-intent class.
// inline (not constexpr) so load_and_apply_weights can overwrite at runtime.
struct WeightProfile {
  double w_metastream;       // T1
  double w_span;             // T2
  double w_quality;          // T3
  double w_bm25;             // T4
  double w_title_coverage;   // T5
  int max_per_host;          // host collapsing limit (0 = unlimited)
};

// defaults are zero — must load from config/weights.txt before ranking
inline WeightProfile GENERAL      = {0.0, 0.0, 0.0, 0.0, 0.0, 2};
inline WeightProfile NAVIGATIONAL = {0.0, 0.0, 0.0, 0.0, 0.0, 0};

// power-weight alpha for static^α × dynamic^(1-α) combination.
// 0.5 = equal weight, <0.5 = favor dynamic (relevance), >0.5 = favor static (quality)
inline double COMBINE_ALPHA = 0.4;

// static score floor — prevents low-quality urls from zeroing out relevant pages
inline double STATIC_FLOOR = 0.15;

// t1 metastream field weights (best-of per query term)
inline double W_FIELD_URL   = 1.0;
inline double W_FIELD_TITLE = 0.8;
inline double W_FIELD_BODY  = 0.3;

// t2 span title/body blend
inline double SPAN_TITLE_WEIGHT = 0.7;

// bm25 sigmoid normalization steepness
inline double SIGMOID_K = 0.1;

// content quality penalty thresholds
inline int    TITLE_MAX_REPEAT = 3;
inline double NON_LATIN_PENALTY_SCALE = 2.0;  // multiplied by non-latin ratio
inline double NON_LATIN_FLOOR = 0.2;          // minimum penalty (max suppression)

// shard-side tuning (only used when running as a shard)
inline double SHARD_GOOD_THRESHOLD = 0.3;   // per-result score to count as "good"
inline int    SHARD_GOOD_COUNT = 30;        // good results before early-stop
inline int    SHARD_MAX_RESULTS = 100;      // per-level cap on emitted results

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
    else if (key == "profile.general.bm25")            GENERAL.w_bm25            = value;
    else if (key == "profile.general.title_coverage")  GENERAL.w_title_coverage  = value;
    else if (key == "profile.general.max_per_host")   GENERAL.max_per_host     = static_cast<int>(value);
    else if (key == "profile.navigational.metastream") NAVIGATIONAL.w_metastream = value;
    else if (key == "profile.navigational.span")       NAVIGATIONAL.w_span       = value;
    else if (key == "profile.navigational.quality")    NAVIGATIONAL.w_quality    = value;
    else if (key == "profile.navigational.bm25")       NAVIGATIONAL.w_bm25       = value;
    else if (key == "profile.navigational.title_coverage") NAVIGATIONAL.w_title_coverage = value;
    else if (key == "profile.navigational.max_per_host") NAVIGATIONAL.max_per_host = static_cast<int>(value);

    else if (key == "combine_alpha")                   COMBINE_ALPHA             = value;
    else if (key == "static_floor")                    STATIC_FLOOR              = value;
    else if (key == "field.url")                       W_FIELD_URL               = value;
    else if (key == "field.title")                     W_FIELD_TITLE             = value;
    else if (key == "field.body")                      W_FIELD_BODY              = value;
    else if (key == "span.title_weight")               SPAN_TITLE_WEIGHT         = value;
    else if (key == "sigmoid_k")                       SIGMOID_K                 = value;
    else if (key == "penalty.title_max_repeat")        TITLE_MAX_REPEAT          = static_cast<int>(value);
    else if (key == "penalty.non_latin_scale")         NON_LATIN_PENALTY_SCALE   = value;
    else if (key == "penalty.non_latin_floor")         NON_LATIN_FLOOR           = value;

    else if (key == "shard.good_threshold")            SHARD_GOOD_THRESHOLD      = value;
    else if (key == "shard.good_count")                SHARD_GOOD_COUNT          = static_cast<int>(value);
    else if (key == "shard.max_results")               SHARD_MAX_RESULTS         = static_cast<int>(value);

    else std::cerr << "[weights] unknown key: " << key << "\n";
  }
  return true;
}

// serialize current weight globals into a compact semicolon-delimited string
// for sending over the wire. format: "key=val;key=val;..."
// i'm almost certain there's a better way than this but honestly i'm too tired to think of it
inline std::string serialize_weights() {
  std::ostringstream s;
  s << "tld=" << T1_WEIGHTS[0]
    << ";url_len=" << T1_WEIGHTS[1]
    << ";path_depth=" << T1_WEIGHTS[2]
    << ";subdomain_depth=" << T1_WEIGHTS[3]
    << ";ip_in_url=" << T1_WEIGHTS[4]
    << ";special_char_density=" << T1_WEIGHTS[5]
    << ";blacklist=" << T1_WEIGHTS[6]
    << ";profile.general.metastream=" << GENERAL.w_metastream
    << ";profile.general.span=" << GENERAL.w_span
    << ";profile.general.quality=" << GENERAL.w_quality
    << ";profile.general.bm25=" << GENERAL.w_bm25
    << ";profile.general.title_coverage=" << GENERAL.w_title_coverage
    << ";profile.general.max_per_host=" << GENERAL.max_per_host
    << ";profile.navigational.metastream=" << NAVIGATIONAL.w_metastream
    << ";profile.navigational.span=" << NAVIGATIONAL.w_span
    << ";profile.navigational.quality=" << NAVIGATIONAL.w_quality
    << ";profile.navigational.bm25=" << NAVIGATIONAL.w_bm25
    << ";profile.navigational.title_coverage=" << NAVIGATIONAL.w_title_coverage
    << ";profile.navigational.max_per_host=" << NAVIGATIONAL.max_per_host
    << ";combine_alpha=" << COMBINE_ALPHA
    << ";static_floor=" << STATIC_FLOOR
    << ";field.url=" << W_FIELD_URL
    << ";field.title=" << W_FIELD_TITLE
    << ";field.body=" << W_FIELD_BODY
    << ";span.title_weight=" << SPAN_TITLE_WEIGHT
    << ";sigmoid_k=" << SIGMOID_K
    << ";penalty.title_max_repeat=" << TITLE_MAX_REPEAT
    << ";penalty.non_latin_scale=" << NON_LATIN_PENALTY_SCALE
    << ";penalty.non_latin_floor=" << NON_LATIN_FLOOR
    << ";shard.good_threshold=" << SHARD_GOOD_THRESHOLD
    << ";shard.good_count=" << SHARD_GOOD_COUNT
    << ";shard.max_results=" << SHARD_MAX_RESULTS;
  return s.str();
}

// apply weights from a serialized string (format: "key=val;key=val;...")
inline bool apply_weights_from_string(const std::string& data) {
  size_t pos = 0;
  while (pos < data.size()) {
    size_t sep = data.find(';', pos);
    std::string pair = data.substr(pos, sep == std::string::npos
                                          ? std::string::npos : sep - pos);
    pos = (sep == std::string::npos) ? data.size() : sep + 1;

    size_t eq = pair.find('=');
    if (eq == std::string::npos) continue;

    std::string key = pair.substr(0, eq);
    double value;
    try { value = std::stod(pair.substr(eq + 1)); } catch (...) { continue; }

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
    else if (key == "profile.general.bm25")            GENERAL.w_bm25            = value;
    else if (key == "profile.general.title_coverage")  GENERAL.w_title_coverage  = value;
    else if (key == "profile.general.max_per_host")   GENERAL.max_per_host     = static_cast<int>(value);
    else if (key == "profile.navigational.metastream") NAVIGATIONAL.w_metastream = value;
    else if (key == "profile.navigational.span")       NAVIGATIONAL.w_span       = value;
    else if (key == "profile.navigational.quality")    NAVIGATIONAL.w_quality    = value;
    else if (key == "profile.navigational.bm25")       NAVIGATIONAL.w_bm25       = value;
    else if (key == "profile.navigational.title_coverage") NAVIGATIONAL.w_title_coverage = value;
    else if (key == "profile.navigational.max_per_host") NAVIGATIONAL.max_per_host = static_cast<int>(value);

    else if (key == "combine_alpha")                   COMBINE_ALPHA             = value;
    else if (key == "static_floor")                    STATIC_FLOOR              = value;
    else if (key == "field.url")                       W_FIELD_URL               = value;
    else if (key == "field.title")                     W_FIELD_TITLE             = value;
    else if (key == "field.body")                      W_FIELD_BODY              = value;
    else if (key == "span.title_weight")               SPAN_TITLE_WEIGHT         = value;
    else if (key == "sigmoid_k")                       SIGMOID_K                 = value;
    else if (key == "penalty.title_max_repeat")        TITLE_MAX_REPEAT          = static_cast<int>(value);
    else if (key == "penalty.non_latin_scale")         NON_LATIN_PENALTY_SCALE   = value;
    else if (key == "penalty.non_latin_floor")         NON_LATIN_FLOOR           = value;

    else if (key == "shard.good_threshold")            SHARD_GOOD_THRESHOLD      = value;
    else if (key == "shard.good_count")                SHARD_GOOD_COUNT          = static_cast<int>(value);
    else if (key == "shard.max_results")               SHARD_MAX_RESULTS         = static_cast<int>(value);
  }
  return true;
}
