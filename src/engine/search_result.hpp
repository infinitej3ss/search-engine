#pragma once

// shared result type used by both leader and shard sides

#include <string>
#include <vector>

struct SearchResult {
    int doc_id;
    std::string url;
    std::string title;
    std::string snippet;
    double static_score;
    double dynamic_score;
    double combined_score;
    // per-signal breakdown populated by local search. shards will also fill
    // these; when the leader aggregates, they round-trip with the result
    double t1 = 0.0; // metastream (field presence)
    double t2 = 0.0; // span / proximity
    double t3 = 0.0; // hop-distance quality
    double bm25 = 0.0; // normalized bm25
};

// per-query diagnostics for the right-rail debug widgets. populated in
// local mode directly by SearchEngine; in distributed mode the leader
// aggregates per-shard stats via merge()
struct SearchStats {
    std::vector<std::string> parsed_tokens; // post-compile query terms
    std::string parsed_query_ast; // s-expression of the parsed query
    int constraint_solved = 0; // total AND matches across ranks
    int passed_static_floor = 0; // candidates that survived the s>0 filter
    std::vector<int> per_rank_matched; // per_rank_matched[r] = survivors from rank r

    // combine another shard's stats into this one. parsed_tokens is taken
    // from whichever side has it first (all shards compile the same query);
    // counts are summed; per_rank_matched is summed element-wise, resizing
    // to the wider of the two
    void merge(const SearchStats& other) {
        if (parsed_tokens.empty()) parsed_tokens = other.parsed_tokens;
        if (parsed_query_ast.empty()) parsed_query_ast = other.parsed_query_ast;
        constraint_solved += other.constraint_solved;
        passed_static_floor += other.passed_static_floor;
        if (per_rank_matched.size() < other.per_rank_matched.size()) {
            per_rank_matched.resize(other.per_rank_matched.size(), 0);
        }
        for (size_t i = 0; i < other.per_rank_matched.size(); i++) {
            per_rank_matched[i] += other.per_rank_matched[i];
        }
    }
};
