#pragma once

#include <cstddef>

// shared result type for span / proximity scoring. produced by ISR tree
// matches and consumed by the dynamic ranker's T2 scorer
struct SpanResult {
  bool all_terms_present = false;
  size_t shortest_span = 0;
  bool in_order = false;
  bool exact_phrase = false;
};
