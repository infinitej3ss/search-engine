#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>

#include "span_finder.hpp"
#include "weight_profiles.hpp"

// minimal view of a document for dynamic ranking. mirrors the subset of
// parser/crawler DocInfo the ranker actually consumes.
// TODO replace with parser team's DocInfo once finalized
struct DocCandidate {
  uint32_t doc_id = 0;
  std::string url;
  std::vector<std::string> title_words;
  std::vector<std::string> anchor_texts;
  int hop_distance = -1;         // -1 if unknown
  int64_t crawl_timestamp = 0;   // unix seconds, 0 if unknown
  std::string domain;
};

// scans field tokens and returns positions_per_term for a query.
// out[i] = sorted positions where query[i] appears in field.
// O(query × field). fine for short fields; revisit for body.
// TODO case-insensitive compare once we pick a tokenizer
inline std::vector<std::vector<size_t>> build_positions(
    const std::vector<std::string>& query,
    const std::vector<std::string>& field) {
  std::vector<std::vector<size_t>> out(query.size());

  for (size_t i = 0; i < query.size(); i++) {
    for (size_t j = 0; j < field.size(); j++) {
      if (field[j] == query[i]) out[i].push_back(j);
    }
  }

  return out;
}

// TODO T1 — real metastream scorer (title_hit + anchor_hit + url_hit)
inline double t1_metastream(
    const std::vector<std::string>& /* query */,
    const DocCandidate& /* doc */) {
  return 0.5;
}

// T2 — span / proximity. runs span_finder over title_words for now.
// formula per brainstorm §T2:
//   T2 = all_terms × (0.3 + 0.3·exact + 0.25·in_order + 0.15/span)
// TODO extend to body_words when parser ships body data
// TODO also compute over anchor_texts and combine
inline double t2_span(
    const std::vector<std::string>& query,
    const DocCandidate& doc) {
  if (query.empty()) return 0.0;

  auto positions = build_positions(query, doc.title_words);
  auto span = find_span(positions);

  if (!span.all_terms_present) return 0.0;

  double score = 0.3;
  if (span.exact_phrase) score += 0.3;
  if (span.in_order)     score += 0.25;

  // span == 0 only for single-term queries — treat as max proximity
  if (span.shortest_span > 0) {
    score += 0.15 / static_cast<double>(span.shortest_span);
  } else {
    score += 0.15;
  }

  return score;
}

// TODO T3 — light global quality: 0.7·static_score + 0.3·hop_distance
inline double t3_quality(const DocCandidate& /* doc */) {
  return 0.5;
}

// TODO T4 — freshness: 1 / (1 + days_since/30) when timestamp != 0
inline double t4_freshness(const DocCandidate& /* doc */) {
  return 0.5;
}

// top-level dynamic score = w1·T1 + w2·T2 + w3·T3 + w4·T4
inline double score_dynamic(
    const std::vector<std::string>& query,
    const DocCandidate& doc,
    const WeightProfile& profile) {
  double t1 = t1_metastream(query, doc);
  double t2 = t2_span(query, doc);
  double t3 = t3_quality(doc);
  double t4 = t4_freshness(doc);

  return profile.w_metastream * t1
       + profile.w_span       * t2
       + profile.w_quality    * t3
       + profile.w_freshness  * t4;
}
