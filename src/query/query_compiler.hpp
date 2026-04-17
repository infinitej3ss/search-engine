#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

// v1 query compiler: lowercase + tokenize on non-alphanumeric.
// implicit AND is enforced downstream by the ranker (t2_span zeroes out
// when not all query terms are present), so this layer just produces a
// clean list of terms.
namespace query {

inline std::vector<std::string> compile(std::string_view raw) {
  std::vector<std::string> terms;
  std::string buf;
  buf.reserve(32);

  auto flush = [&] {
    if (!buf.empty()) {
      terms.push_back(std::move(buf));
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

  return terms;
}

}  // namespace query
