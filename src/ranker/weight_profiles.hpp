#pragma once

// v1 tunable weights. tune via eval from constraint solver group.
// TODO per-profile sub-weights for anchor/url/title inside T1 (brainstorm §T1)

struct WeightProfile {
  double w_metastream;  // T1
  double w_span;        // T2
  double w_quality;     // T3
};

constexpr WeightProfile GENERAL      = {0.35, 0.45, 0.20};
constexpr WeightProfile NAVIGATIONAL = {0.60, 0.15, 0.25};
