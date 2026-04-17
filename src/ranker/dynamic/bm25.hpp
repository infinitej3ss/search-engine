#pragma once

// bm25 scorer for query-time ranking.
// adapted from the original batch bm25 class to work with index stats
// (document frequency, total doc count) rather than scanning all docs.

#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>
#include <cctype>

class BM25 {
public:
    static constexpr double k = 1.2;
    static constexpr double b = 0.75;

    // construct with corpus-level stats (computed once at index load)
    BM25(int total_docs, double avg_doc_length)
        : n_docs(total_docs), avg_length(avg_doc_length) {}

    // score a single document against query terms.
    // doc_freq_fn: returns how many docs contain a given term (from index)
    // doc_words: the document's body words
    // query: the query terms (already lowercased by query compiler)
    double score(const std::vector<std::string>& doc_words,
                 const std::vector<std::string>& query,
                 const std::unordered_map<std::string, int>& doc_freq) const {
        if (doc_words.empty() || query.empty()) return 0.0;

        // build term frequency map for this doc (lowercase)
        std::unordered_map<std::string, size_t> tf;
        for (const auto& w : doc_words) {
            std::string lower;
            lower.reserve(w.size());
            for (char c : w)
                lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            ++tf[lower];
        }

        double doc_len = static_cast<double>(doc_words.size());
        double total = 0.0;

        for (const auto& term : query) {
            // idf: log(1 + (N - df + 0.5) / (df + 0.5))
            int df = 0;
            auto it = doc_freq.find(term);
            if (it != doc_freq.end()) df = it->second;

            double idf_val = std::log(1.0 + (n_docs - df + 0.5) / (df + 0.5));

            // tf in this doc
            size_t f = 0;
            auto tf_it = tf.find(term);
            if (tf_it != tf.end()) f = tf_it->second;

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
