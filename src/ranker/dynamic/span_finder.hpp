#pragma once

#include <vector>
#include <cstddef>
#include <limits>
#include <algorithm>

// TODO move to .cpp if this grows past header-only comfort

struct SpanResult {
  bool all_terms_present;
  size_t shortest_span;
  bool in_order;
  bool exact_phrase;
};

// finds the tightest window covering every query term, checks term order,
// and flags exact phrases. positions[i] = sorted positions of the i-th query
// term in the doc. caller dedupes query tokens — duplicates not v1.
// TODO handle duplicate query tokens, e.g. "new york new"
inline SpanResult find_span(const std::vector<std::vector<size_t>>& positions) {
  const size_t n = positions.size();

  if (n == 0) return {false, 0, false, false};

  for (const auto& p : positions) {
    if (p.empty()) return {false, 0, false, false};
  }

  if (n == 1) return {true, 0, true, true};

  // merge every (position, term) hit and sort by position
  // TODO field-tagged Hit (title/anchor/body) so metastream can weight by field
  struct Hit { size_t pos; size_t term; };
  std::vector<Hit> hits;

  for (size_t t = 0; t < n; t++) {
    for (size_t p : positions[t]) hits.push_back({p, t});
  }

  std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
    return a.pos < b.pos;
  });

  // min-window substring — slide right, shrink left whenever every term is
  // covered, record the smallest span seen
  // TODO rarest-word anchoring for speed on long docs (brainstorm §T2)
  std::vector<size_t> count(n, 0);
  size_t covered = 0;
  size_t best_span = std::numeric_limits<size_t>::max();
  size_t best_left = 0, best_right = 0;
  size_t left = 0;

  for (size_t right = 0; right < hits.size(); right++) {
    if (count[hits[right].term]++ == 0) covered++;

    while (covered == n) {
      size_t span = hits[right].pos - hits[left].pos;

      if (span < best_span) {
        best_span = span;
        best_left = left;
        best_right = right;
      }

      if (--count[hits[left].term] == 0) covered--;
      left++;
    }
  }

  // in_order — first-seen position per term in the best window must rise
  // with term index
  const size_t none = std::numeric_limits<size_t>::max();
  std::vector<size_t> first_pos(n, none);

  for (size_t i = best_left; i <= best_right; i++) {
    if (first_pos[hits[i].term] == none) first_pos[hits[i].term] = hits[i].pos;
  }

  bool in_order = true;
  for (size_t t = 1; t < n; t++) {
    if (first_pos[t] < first_pos[t - 1]) { in_order = false; break; }
  }

  bool exact_phrase = (best_span == n - 1) && in_order;

  return {true, best_span, in_order, exact_phrase};
}
