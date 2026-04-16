#pragma once

// core search logic: index management, query execution, ranking.
// server/plugin layer calls into this — keeps http concerns separate.

#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cstdint>

#include "../../index/simple_api.h"
#include "../../config/weights.hpp"
#include "../ranker/static/static_ranker.hpp"
#include "../ranker/dynamic/dynamic_ranker.hpp"

struct SearchResult {
    int doc_id;
    std::string url;
    double static_score;
    double dynamic_score;
    double combined_score;
};

class SearchEngine {
private:
    IndexHandle index;
    std::string weights_path;

    void load_dummy_data() {
        // placeholder data for development. swap this out for
        // build_index_from_crawler() once crawl data is available.
        struct Doc {
            const char* url;
            std::vector<const char*> title;
            std::vector<const char*> body;
            int distance;
        };

        Doc docs[] = {
            {
                "https://en.wikipedia.org/wiki/Cat",
                {"cats", "wikipedia"},
                {"cats", "are", "small", "domesticated", "feline", "mammals"},
                0
            },
            {
                "https://www.petfinder.com/cats/",
                {"adopt", "a", "cat"},
                {"find", "cats", "for", "adoption", "near", "you", "pets", "animals"},
                1
            },
            {
                "https://example.com/dogs",
                {"dogs", "canine"},
                {"dogs", "are", "friendly", "loyal", "pets"},
                1
            },
            {
                "https://en.wikipedia.org/wiki/Pet",
                {"pet", "wikipedia"},
                {"a", "pet", "is", "an", "animal", "kept", "for", "companionship",
                 "cats", "dogs", "are", "common", "pets"},
                2
            },
            {
                "https://news.ycombinator.com",
                {"hacker", "news"},
                {"technology", "startups", "programming", "science"},
                3
            },
        };

        for (auto& doc : docs) {
            add_document(index, doc.url,
                doc.title.data(), static_cast<int>(doc.title.size()),
                doc.body.data(), static_cast<int>(doc.body.size()),
                doc.distance);
        }

        finalize_index(index);
        std::cerr << "[engine] loaded " << get_document_count(index)
                  << " documents" << std::endl;
    }

    double compute_static_score(const char* url) {
        if (!url) return 0.0;

        RankerInput rinput;
        rinput.url = url;
        rinput.is_https = (std::string(url).find("https://") == 0);
        rinput.pages_per_domain = 0;
        rinput.hop_distance = 0;
        rinput.word_count = 0;
        rinput.content_to_html_ratio = 0.0;

        StaticRanker ranker(rinput);
        double s = ranker.rank();
        return s < 0.0 ? 0.0 : s;
    }

    double compute_dynamic_score(const std::vector<std::string>& terms,
                                  int doc_id, const char* url) {
        DocCandidate candidate;
        candidate.doc_id = static_cast<uint32_t>(doc_id);
        candidate.url = url ? url : "";
        candidate.hop_distance = -1;

        return score_dynamic(terms, candidate, GENERAL);
    }

public:
    SearchEngine(const std::string& weights_file)
        : weights_path(weights_file) {
        index = create_index();

        if (!load_and_apply_weights(weights_path)) {
            std::cerr << "[engine] warning: could not load " << weights_path
                      << ", all weights are zero" << std::endl;
        }

        load_dummy_data();
    }

    ~SearchEngine() {
        destroy_index(index);
    }

    // runs a query: reload weights, hit the index, rank, sort, return
    std::vector<SearchResult> search(const std::vector<std::string>& terms) {
        std::vector<SearchResult> results;
        if (terms.empty()) return results;

        // reload weights on every query so edits take effect live
        load_and_apply_weights(weights_path);

        // build the c-style array the index api expects
        std::vector<const char*> c_terms;
        for (const auto& t : terms) {
            c_terms.push_back(t.c_str());
        }

        int result_count = 0;
        int* doc_ids = find_and_query(index, c_terms.data(),
            static_cast<int>(c_terms.size()), &result_count);

        std::cerr << "[engine] query terms=" << terms.size()
                  << " results=" << result_count << std::endl;

        for (int i = 0; i < result_count; i++) {
            int doc_id = doc_ids[i];
            const char* url = get_document_url(index, doc_id);

            double ss = compute_static_score(url);
            double ds = compute_dynamic_score(terms, doc_id, url);

            // multiplicative combination — matches ranker team's approach
            double combined = ss * ds;

            results.push_back({doc_id, url ? url : "", ss, ds, combined});
        }

        free_results(doc_ids);

        std::sort(results.begin(), results.end(),
            [](const SearchResult& a, const SearchResult& b) {
                return a.combined_score > b.combined_score;
            });

        return results;
    }
};
