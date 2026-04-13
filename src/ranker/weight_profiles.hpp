#pragma once

// v1 tunable weights. tune via eval from constraint solver group.
// TODO per-profile sub-weights for anchor/url/title inside T1 (brainstorm §T1)

struct WeightProfile {
  double w_metastream;  // T1
  double w_span;        // T2
  double w_quality;     // T3
  double w_freshness;   // T4
};

constexpr WeightProfile GENERAL      = {0.35, 0.40, 0.20, 0.05};
constexpr WeightProfile NAVIGATIONAL = {0.60, 0.10, 0.25, 0.05};
constexpr WeightProfile FRESHNESS    = {0.30, 0.30, 0.15, 0.25};
