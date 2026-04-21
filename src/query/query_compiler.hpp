#pragma once

// thin public entry point for query compilation. parsing, normalization,
// term extraction, and AST→ISR compilation all live in query_ast.hpp.
// callers get back both the AST (for retrieval) and the flattened terms
// list (for BM25 / T1 / ranker signals) in one shot.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "query_ast.hpp"

namespace query {

struct CompiledQuery {
  std::unique_ptr<QueryNode> ast;
  std::vector<std::string> terms;

  bool empty() const { return !ast || terms.empty(); }
};

// parse + normalize + extract terms. a query with no word characters at
// all produces a CompiledQuery whose ast is null and terms is empty —
// callers use .empty() to detect this
inline CompiledQuery compile(std::string_view raw) {
  CompiledQuery out;
  out.ast = parse(raw);
  if (!out.ast) return out;
  normalize(*out.ast);
  out.terms = extract_terms(*out.ast);
  return out;
}

}  // namespace query
