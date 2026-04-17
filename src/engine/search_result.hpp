#pragma once

// shared result type used by both leader and shard sides

#include <string>

struct SearchResult {
    int doc_id;
    std::string url;
    double static_score;
    double dynamic_score;
    double combined_score;
};
