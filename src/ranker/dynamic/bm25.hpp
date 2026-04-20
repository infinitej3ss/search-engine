#pragma once

// bm25 scorer for query-time ranking. takes precomputed per-doc signals
// (body_length, body_tf) rather than raw doc_words, so the index doesn't
// need to store body text

#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>

class BM25 {
public:
    static constexpr double k = 1.2;
    static constexpr double b = 0.75;

    // construct with corpus-level stats (computed once at index load)
    BM25(int total_docs, double avg_doc_length)
        : n_docs(total_docs), avg_length(avg_doc_length) {}

    // score a single document against query terms
    // body_length: the doc's raw body word count (for length normalization)
    // body_tf: per-term frequency within the doc body. caller populates by
    //          walking the index with GetFieldTermFrequency(doc, term, 'b')
    // query: the query terms (already lowercased by query compiler)
    // doc_freq: global df per term (from index)
    double score(int body_length,
                 const std::unordered_map<std::string, int>& body_tf,
                 const std::vector<std::string>& query,
                 const std::unordered_map<std::string, int>& doc_freq) const {
        if (body_length <= 0 || query.empty()) return 0.0;

        double doc_len = static_cast<double>(body_length);
        double total = 0.0;

        for (const auto& term : query) {
            // idf: log(1 + (N - df + 0.5) / (df + 0.5))
            int df = 0;
            auto it = doc_freq.find(term);
            if (it != doc_freq.end()) df = it->second;

            double idf_val = std::log(1.0 + (n_docs - df + 0.5) / (df + 0.5));

            // tf in this doc
            int f = 0;
            auto tf_it = body_tf.find(term);
            if (tf_it != body_tf.end()) f = tf_it->second;

            // bm25 term score
            double num = static_cast<double>(f) * (k + 1.0);
            double denom = static_cast<double>(f) + k * (1.0 - b + b * doc_len / avg_length);

            total += idf_val * num / denom;
        }

        return total;
    }

private:
    int n_docs;
    double avg_length;
};
