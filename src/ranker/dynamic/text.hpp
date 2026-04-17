#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

// tokenized text: term -> frequency in this text.
// TODO pick a real tokenizer (case folding, punctuation stripping) — for now,
// callers pass pre-split words and we just build the frequency map.
struct Text {
  std::unordered_map<std::string, size_t> tokens;

  Text() = default;

  explicit Text(const std::vector<std::string>& words) {
    for (const auto& w : words) ++tokens[w];
  }
};

// tokenized document: Text plus its length in tokens (used by BM25's length
// normalization). length is stored instead of recomputed so docs ingested
// from an external tokenizer can report their true length.
struct Document : Text {
  size_t length = 0;

  Document() = default;

  explicit Document(const std::vector<std::string>& words)
      : Text(words), length(words.size()) {}

  size_t get_length() const { return length; }
};
