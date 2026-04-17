#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// v1 query compiler: lowercase + tokenize on non-alphanumeric, then drop
// stop words and de-duplicate. Implicit AND is enforced downstream by the
// ranker (t2_span zeroes out when not all query terms are present).
namespace query {

// Common English stop words. Kept short on purpose — the ranker's T2 span
// scorer tolerates stop-word-heavy phrases, so we only drop the ones that
// show up in almost every document and carry no retrieval signal.
inline const std::unordered_set<std::string>& stop_words() {
  static const std::unordered_set<std::string> s = {
    "a", "an", "and", "or", "but", "the", "is", "are", "was", "were",
    "be", "been", "being", "to", "of", "in", "on", "at", "for", "with",
    "by", "from", "as", "this", "that", "these", "those", "it", "its",
    "if", "then", "so", "do", "does", "did", "has", "have", "had",
  };
  return s;
}

inline std::vector<std::string> compile(std::string_view raw) {
  std::vector<std::string> tokens;
  std::string buf;
  buf.reserve(32);

  auto flush = [&] {
    if (!buf.empty()) {
      tokens.push_back(std::move(buf));
      buf.clear();
    }
  };

  for (unsigned char c : raw) {
    if (std::isalnum(c)) {
      buf.push_back(static_cast<char>(std::tolower(c)));
    } else {
      flush();
    }
  }
  flush();

  // Filter stop words. If stripping leaves nothing (e.g. "to be or not to
  // be"), fall back to the un-filtered tokens so the query isn't dropped.
  const auto& stops = stop_words();
  std::vector<std::string> filtered;
  filtered.reserve(tokens.size());
  for (auto& t : tokens) {
    if (!stops.count(t)) filtered.push_back(std::move(t));
  }
  std::vector<std::string>& kept = filtered.empty() ? tokens : filtered;

  // De-duplicate, preserving first-occurrence order. O(n) with a small set.
  std::unordered_set<std::string> seen;
  seen.reserve(kept.size());
  std::vector<std::string> out;
  out.reserve(kept.size());
  for (auto& t : kept) {
    if (seen.insert(t).second) out.push_back(std::move(t));
  }
  return out;
}

}  // namespace query
