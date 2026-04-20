#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <cstddef>
#include <cctype>
#include <algorithm>

#include "span_finder.hpp"
#include "../../../config/weights.hpp"

// minimal view of a document for dynamic ranking
// body_tf / body_positions are per-query signals derived from the index at
// query time (via Index::GetFieldTermFrequency / GetFieldPositions). title
// is kept as a word list because title_words stays in DocumentMetadata
struct DocCandidate {
  uint32_t doc_id = 0;
  std::string url;
  std::vector<std::string> title_words;
  int hop_distance = -1;
  int body_length = 0;
  std::string domain;
  // per-query signals; populated by the search engine before scoring
  std::unordered_map<std::string, int> body_tf;
  std::unordered_map<std::string, std::vector<size_t>> body_positions;
};

// lowercase a string for case-insensitive matching.
// query compiler lowercases queries; index data may have mixed case.
inline std::string to_lower(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s)
    out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

// tokenize a url into lowercase alphanumeric parts for matching.
// same approach as Index::splitURL.
inline std::vector<std::string> tokenize_url(const std::string& url) {
  std::vector<std::string> parts;
  std::string current;
  for (char c : url) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      current += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (!current.empty()) {
      parts.push_back(current);
      current.clear();
    }
  }
  if (!current.empty()) parts.push_back(current);
  return parts;
}

// scans field tokens and returns positions_per_term for a query.
// out[i] = sorted positions where query[i] appears in field.
// case-insensitive since query is lowercased but field data may not be.
inline std::vector<std::vector<size_t>> build_positions(
    const std::vector<std::string>& query,
    const std::vector<std::string>& field) {
  std::vector<std::vector<size_t>> out(query.size());

  for (size_t i = 0; i < query.size(); i++) {
    for (size_t j = 0; j < field.size(); j++) {
      if (to_lower(field[j]) == query[i]) out[i].push_back(j);
    }
  }

  return out;
}

// t1 — metastream scorer: checks if query terms appear in url, title, body.
// per lecture, field importance: anchor > url > title > body.
// anchor is not populated in current crawl data, so omitted for v1.
// returns a score in [0, 1] based on how many terms hit and where.
// best-of per term — a term hitting multiple fields takes the highest weight.
inline double t1_metastream(
    const std::vector<std::string>& query,
    const DocCandidate& doc) {
  if (query.empty()) return 0.0;

  auto url_tokens = tokenize_url(doc.url);

  double total = 0.0;

  for (const auto& term : query) {
    double best = 0.0;

    for (const auto& w : url_tokens) {
      if (w == term) { best = W_FIELD_URL; break; }
    }

    if (best < W_FIELD_TITLE) {
      for (const auto& w : doc.title_words) {
        if (to_lower(w) == term) { best = std::max(best, W_FIELD_TITLE); break; }
      }
    }

    if (best < W_FIELD_BODY) {
      auto it = doc.body_tf.find(term);
      if (it != doc.body_tf.end() && it->second > 0) best = std::max(best, W_FIELD_BODY);
    }

    total += best;
  }

  return total / static_cast<double>(query.size());
}

// span score formula: all_terms × (0.3 + 0.3·exact + 0.25·in_order + 0.15/span)
inline double span_score_from_positions(
    const std::vector<std::vector<size_t>>& positions) {
  auto span = find_span(positions);
  if (!span.all_terms_present) return 0.0;

  double score = 0.3;
  if (span.exact_phrase) score += 0.3;
  if (span.in_order)     score += 0.25;

  if (span.shortest_span > 0) {
    score += 0.15 / static_cast<double>(span.shortest_span);
  } else {
    score += 0.15;
  }
  return score;
}

// span score over a raw word list (still used for title, which stays in
// DocCandidate as title_words)
inline double span_score_for_field(
    const std::vector<std::string>& query,
    const std::vector<std::string>& field) {
  if (query.empty() || field.empty()) return 0.0;
  return span_score_from_positions(build_positions(query, field));
}

// t2 — span / proximity. title is scanned locally; body uses index-derived
// positions pre-populated on the candidate
inline double t2_span(
    const std::vector<std::string>& query,
    const DocCandidate& doc) {
  if (query.empty()) return 0.0;

  double title_span = span_score_for_field(query, doc.title_words);

  // translate doc.body_positions (keyed by term) into a parallel
  // positions_per_term vector aligned with `query`
  std::vector<std::vector<size_t>> body_pos(query.size());
  for (size_t i = 0; i < query.size(); i++) {
    auto it = doc.body_positions.find(query[i]);
    if (it != doc.body_positions.end()) body_pos[i] = it->second;
  }
  double body_span = span_score_from_positions(body_pos);

  // title proximity weighted higher — a tight span in the title is
  // a stronger signal than in the body
  return SPAN_TITLE_WEIGHT * title_span + (1.0 - SPAN_TITLE_WEIGHT) * body_span;
}

// t3 — light global quality signal from hop distance.
// pages closer to the seed list (lower hop distance) are more trustworthy.
// returns score in [0, 1].
inline double t3_quality(const DocCandidate& doc) {
  if (doc.hop_distance < 0) return 0.5; // unknown, neutral
  return 1.0 / (1.0 + doc.hop_distance);
}

// content quality penalty: detects non-english content and keyword stuffing.
// returns a multiplier in (0, 1]. 1.0 = no penalty.
inline double content_quality_penalty(const DocCandidate& doc) {
  double penalty = 1.0;

  // non-latin character detection in title.
  // check code points > U+024F (end of latin extended blocks)
  int non_latin = 0;
  int total_chars = 0;
  for (const auto& word : doc.title_words) {
    for (unsigned char c : word) {
      total_chars++;
      if (c > 0x7E) non_latin++; // non-ascii byte
    }
  }
  if (total_chars > 0 && non_latin > 0) {
    double non_latin_ratio = static_cast<double>(non_latin) / total_chars;
    // any non-latin in title is a strong signal; scale penalty with ratio
    penalty *= std::max(NON_LATIN_FLOOR, 1.0 - non_latin_ratio * NON_LATIN_PENALTY_SCALE);
  }

  // keyword stuffing in title: cap max repetitions of any single term
  if (!doc.title_words.empty()) {
    std::unordered_map<std::string, int> title_freq;
    for (const auto& w : doc.title_words) title_freq[to_lower(w)]++;

    int max_freq = 0;
    for (const auto& [word, count] : title_freq)
      if (count > max_freq) max_freq = count;

    if (max_freq > TITLE_MAX_REPEAT) {
      penalty *= static_cast<double>(TITLE_MAX_REPEAT) / max_freq;
    }
  }

  return penalty;
}

// top-level dynamic score = w1·T1 + w2·T2 + w3·T3 + w4·T4
// bm25_score is precomputed by the caller (needs index-level stats)
// and passed in. normalized to [0,1] by the caller.
// content quality penalty is applied as a multiplier.
inline double score_dynamic(
    const std::vector<std::string>& query,
    const DocCandidate& doc,
    const WeightProfile& profile,
    double bm25_score = 0.0) {
  double t1 = t1_metastream(query, doc);
  double t2 = t2_span(query, doc);
  double t3 = t3_quality(doc);

  double raw = profile.w_metastream * t1
             + profile.w_span       * t2
             + profile.w_quality    * t3
             + profile.w_bm25      * bm25_score;

  return raw * content_quality_penalty(doc);
}
