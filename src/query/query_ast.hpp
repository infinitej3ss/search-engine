#pragma once

// query ast + parser + isr-tree compiler.
//
// grammar (recursive descent, left-associative where it matters):
//   query    := or
//   or       := and ( '|' and )*
//   and      := unary ( WS+ unary )*             // implicit AND on whitespace
//   unary    := '-' primary | primary
//   primary  := '(' or ')' | phrase | word
//   phrase   := '"' word (WS+ word)* '"'
//   word     := [a-zA-Z0-9]+  (lowercased)
//
// the parser is tolerant: unterminated quotes consume the rest of the input
// as phrase body, unbalanced parens auto-close at eof, stray characters
// are skipped. the only way to get a null root is if the input contains
// no word characters at all.

#include <cctype>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "index/index.h"
#include "index/isr.h"

namespace query {

struct QueryNode {
  enum Kind { WORD, PHRASE, AND, OR, NOT };
  Kind kind;
  std::string term;                                 // WORD only
  std::vector<std::unique_ptr<QueryNode>> kids;     // PHRASE/AND/OR/NOT

  static std::unique_ptr<QueryNode> makeWord(std::string t) {
    auto n = std::make_unique<QueryNode>();
    n->kind = WORD;
    n->term = std::move(t);
    return n;
  }
  static std::unique_ptr<QueryNode> makePhrase() {
    auto n = std::make_unique<QueryNode>();
    n->kind = PHRASE;
    return n;
  }
  static std::unique_ptr<QueryNode> makeAnd() {
    auto n = std::make_unique<QueryNode>();
    n->kind = AND;
    return n;
  }
  static std::unique_ptr<QueryNode> makeOr() {
    auto n = std::make_unique<QueryNode>();
    n->kind = OR;
    return n;
  }
  static std::unique_ptr<QueryNode> makeNot(std::unique_ptr<QueryNode> inner) {
    auto n = std::make_unique<QueryNode>();
    n->kind = NOT;
    n->kids.push_back(std::move(inner));
    return n;
  }

  // s-expression pretty print for logs / sidebar display
  std::string to_string() const {
    switch (kind) {
      case WORD: return term;
      case PHRASE: {
        std::string s = "\"";
        for (size_t i = 0; i < kids.size(); i++) {
          if (i > 0) s += " ";
          s += kids[i]->to_string();
        }
        s += "\"";
        return s;
      }
      case AND: {
        std::string s = "AND(";
        for (size_t i = 0; i < kids.size(); i++) {
          if (i > 0) s += ", ";
          s += kids[i]->to_string();
        }
        s += ")";
        return s;
      }
      case OR: {
        std::string s = "OR(";
        for (size_t i = 0; i < kids.size(); i++) {
          if (i > 0) s += ", ";
          s += kids[i]->to_string();
        }
        s += ")";
        return s;
      }
      case NOT: {
        std::string s = "NOT(";
        if (!kids.empty()) s += kids[0]->to_string();
        s += ")";
        return s;
      }
    }
    return "?";
  }
};

// common-english stop words. kept short on purpose — only the ones that
// carry no retrieval signal. the full list lives here because normalize()
// and extract_terms() both consult it
inline const std::unordered_set<std::string>& stop_words() {
  static const std::unordered_set<std::string> s = {
    "a", "an", "and", "or", "but", "the", "is", "are", "was", "were",
    "be", "been", "being", "to", "of", "in", "on", "at", "for", "with",
    "by", "from", "as", "this", "that", "these", "those", "it", "its",
    "if", "then", "so", "do", "does", "did", "has", "have", "had",
  };
  return s;
}

namespace detail {

inline bool is_ws(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
inline bool is_word(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9');
}

class Parser {
 public:
  explicit Parser(std::string_view s) : s_(s), i_(0) {}

  std::unique_ptr<QueryNode> parse_root() {
    skip_ws();
    auto root = parse_or();
    // tolerant: ignore any trailing garbage
    return root;
  }

 private:
  std::string_view s_;
  size_t i_;

  bool eof() const { return i_ >= s_.size(); }
  char peek() const { return eof() ? '\0' : s_[i_]; }
  void bump() { if (!eof()) i_++; }
  void skip_ws() { while (!eof() && is_ws(peek())) bump(); }

  std::string read_word() {
    std::string w;
    while (!eof() && is_word(peek())) {
      w.push_back(static_cast<char>(
          std::tolower(static_cast<unsigned char>(peek()))));
      bump();
    }
    return w;
  }

  std::unique_ptr<QueryNode> parse_or() {
    auto left = parse_and();
    while (true) {
      skip_ws();
      if (peek() != '|') break;
      bump();
      skip_ws();
      auto right = parse_and();
      if (!right) break;
      if (!left) { left = std::move(right); continue; }
      if (left->kind == QueryNode::OR) {
        left->kids.push_back(std::move(right));
      } else {
        auto orN = QueryNode::makeOr();
        orN->kids.push_back(std::move(left));
        orN->kids.push_back(std::move(right));
        left = std::move(orN);
      }
    }
    return left;
  }

  std::unique_ptr<QueryNode> parse_and() {
    skip_ws();
    auto first = parse_unary();
    if (!first) return nullptr;
    std::vector<std::unique_ptr<QueryNode>> kids;
    kids.push_back(std::move(first));
    while (true) {
      skip_ws();
      if (eof() || peek() == '|' || peek() == ')') break;
      auto next = parse_unary();
      if (!next) break;
      kids.push_back(std::move(next));
    }
    if (kids.size() == 1) return std::move(kids[0]);
    auto andN = QueryNode::makeAnd();
    andN->kids = std::move(kids);
    return andN;
  }

  std::unique_ptr<QueryNode> parse_unary() {
    skip_ws();
    if (peek() == '-') {
      bump();
      auto inner = parse_primary();
      if (!inner) return nullptr;
      return QueryNode::makeNot(std::move(inner));
    }
    return parse_primary();
  }

  std::unique_ptr<QueryNode> parse_primary() {
    skip_ws();
    if (eof()) return nullptr;
    if (peek() == '(') {
      bump();
      auto inner = parse_or();
      skip_ws();
      if (peek() == ')') bump();   // tolerant: missing close is fine
      return inner;
    }
    if (peek() == '"') {
      bump();
      return parse_phrase_body();
    }
    if (is_word(peek())) {
      return QueryNode::makeWord(read_word());
    }
    // tolerant: unknown char → skip and retry
    bump();
    return parse_primary();
  }

  std::unique_ptr<QueryNode> parse_phrase_body() {
    auto phrase = QueryNode::makePhrase();
    while (!eof() && peek() != '"') {
      if (is_ws(peek())) { bump(); continue; }
      if (is_word(peek())) {
        phrase->kids.push_back(QueryNode::makeWord(read_word()));
        continue;
      }
      bump();                        // tolerant: skip non-word chars
    }
    if (peek() == '"') bump();
    if (phrase->kids.empty()) return nullptr;
    if (phrase->kids.size() == 1) return std::move(phrase->kids[0]);
    return phrase;
  }
};

}  // namespace detail

inline std::unique_ptr<QueryNode> parse(std::string_view raw) {
  detail::Parser p(raw);
  return p.parse_root();
}

// flatten nested AND/AND and OR/OR; drop stop-word WORD children of AND
// unless that would empty the AND. phrase contents are never touched
inline void normalize(QueryNode& node) {
  if (node.kind != QueryNode::PHRASE) {
    for (auto& c : node.kids) {
      if (c) normalize(*c);
    }
  }

  if (node.kind == QueryNode::AND || node.kind == QueryNode::OR) {
    std::vector<std::unique_ptr<QueryNode>> flat;
    for (auto& c : node.kids) {
      if (!c) continue;
      if (c->kind == node.kind) {
        for (auto& gc : c->kids) flat.push_back(std::move(gc));
      } else {
        flat.push_back(std::move(c));
      }
    }
    node.kids = std::move(flat);
  }

  if (node.kind == QueryNode::AND) {
    const auto& stops = stop_words();
    bool any_non_stop = false;
    for (auto& c : node.kids) {
      if (!(c->kind == QueryNode::WORD && stops.count(c->term))) {
        any_non_stop = true;
        break;
      }
    }
    if (any_non_stop) {
      std::vector<std::unique_ptr<QueryNode>> kept;
      for (auto& c : node.kids) {
        if (c->kind == QueryNode::WORD && stops.count(c->term)) continue;
        kept.push_back(std::move(c));
      }
      node.kids = std::move(kept);
    }
  }
}

namespace detail {

inline void collect_words(const QueryNode& n, std::vector<std::string>& out) {
  if (n.kind == QueryNode::WORD) {
    out.push_back(n.term);
    return;
  }
  for (auto& c : n.kids) collect_words(*c, out);
}

}  // namespace detail

// flatten every WORD in the AST, stop-filter (with fallback if that empties),
// and dedupe in first-seen order. used for BM25 / T1 / ranker signals
inline std::vector<std::string> extract_terms(const QueryNode& root) {
  std::vector<std::string> raw;
  detail::collect_words(root, raw);

  const auto& stops = stop_words();
  std::vector<std::string> filtered;
  filtered.reserve(raw.size());
  for (auto& t : raw) {
    if (!stops.count(t)) filtered.push_back(t);
  }
  std::vector<std::string>& kept = filtered.empty() ? raw : filtered;

  std::unordered_set<std::string> seen;
  seen.reserve(kept.size());
  std::vector<std::string> out;
  out.reserve(kept.size());
  for (auto& t : kept) {
    if (seen.insert(t).second) out.push_back(std::move(t));
  }
  return out;
}

// walk the AST and build the corresponding ISR tree. returns nullptr for
// a degenerate query (empty AST, or NOT-only at root)
inline std::unique_ptr<ISR> compile_to_isr(const QueryNode& node,
                                            const Index* idx) {
  using K = QueryNode::Kind;
  switch (node.kind) {
    case K::WORD: {
      return std::make_unique<ISRWord>(idx, node.term);
    }
    case K::PHRASE: {
      std::vector<std::unique_ptr<ISR>> kids;
      for (auto& c : node.kids) {
        auto inner = compile_to_isr(*c, idx);
        if (inner) kids.push_back(std::move(inner));
      }
      if (kids.empty()) return nullptr;
      if (kids.size() == 1) return std::move(kids[0]);
      return std::make_unique<ISRPhrase>(idx, std::move(kids));
    }
    case K::AND: {
      std::vector<std::unique_ptr<ISR>> contained;
      std::vector<std::unique_ptr<ISR>> excluded;
      for (auto& c : node.kids) {
        if (c->kind == K::NOT) {
          if (!c->kids.empty()) {
            auto inner = compile_to_isr(*c->kids[0], idx);
            if (inner) excluded.push_back(std::move(inner));
          }
        } else {
          auto inner = compile_to_isr(*c, idx);
          if (inner) contained.push_back(std::move(inner));
        }
      }
      if (contained.empty()) return nullptr;   // NOT-only is empty
      if (excluded.empty()) {
        if (contained.size() == 1) return std::move(contained[0]);
        return std::make_unique<ISRAnd>(idx, std::move(contained));
      }
      return std::make_unique<ISRContainer>(idx, std::move(contained),
                                             std::move(excluded));
    }
    case K::OR: {
      std::vector<std::unique_ptr<ISR>> kids;
      for (auto& c : node.kids) {
        auto inner = compile_to_isr(*c, idx);
        if (inner) kids.push_back(std::move(inner));
      }
      if (kids.empty()) return nullptr;
      if (kids.size() == 1) return std::move(kids[0]);
      return std::make_unique<ISROr>(idx, std::move(kids));
    }
    case K::NOT: {
      // lonely NOT — no way to answer without an implicit match-all. empty
      return nullptr;
    }
  }
  return nullptr;
}

}  // namespace query
