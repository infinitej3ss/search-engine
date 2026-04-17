#pragma once

// core search logic. two modes:
// - local: loads index blobs (or demo corpus) in-process
// - distributed: fans out to shards via query_distributor

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "search_result.hpp"
#include "query_distributor.hpp"
#include "../index/index.h"
#include "../index/page_data.h"
#include "../index/constraint_solver.h"
#include "../query/query_compiler.hpp"
#include "../../config/weights.hpp"
#include "../ranker/static/static_ranker.hpp"
#include "../ranker/dynamic/dynamic_ranker.hpp"

static constexpr int DEFAULT_TOP_K = 10;

class SearchEngine {
private:
    // local mode, one index per rank bucket
    std::vector<std::unique_ptr<Index>> indexes;
    std::string weights_path;
    int top_k;

    // distributed mode
    QueryDistributor* distributor = nullptr;

    static DocCandidate to_candidate(const Index::DocumentMetadata& meta) {
        DocCandidate d;
        d.doc_id = static_cast<uint32_t>(meta.doc_id);
        d.url = meta.url;
        d.title_words = meta.title_words;
        d.body_words = meta.body_words;
        d.anchor_texts = meta.anchor_texts;
        d.hop_distance = meta.hop_distance;
        return d;
    }

    static double compute_score(const std::vector<std::string>& terms,
                                 const DocCandidate& cand) {
        RankerInput in;
        in.url = cand.url;
        in.is_https = (cand.url.find("https://") == 0);
        in.pages_per_domain = 0;
        in.hop_distance = static_cast<size_t>(std::max(cand.hop_distance, 0));
        in.word_count = 0;
        in.content_to_html_ratio = 0.0;

        double s = StaticRanker(in).rank();
        if (s < 0.0) return -1.0;
        return s * score_dynamic(terms, cand, GENERAL);
    }

    // demo corpus for when no index blobs are available
    static std::vector<PageData> make_demo_corpus() {
        auto mk = [](uint64_t hop, const std::string& url,
                     std::vector<std::string> title,
                     std::vector<std::string> anchor) {
            PageData p;
            p.distance_from_seedlist = hop;
            p.url = url;
            p.titlewords = title;
            p.words = title;
            p.anchor_text = std::move(anchor);
            return p;
        };
        return {
            mk(1, "https://docs.python.org/3/tutorial/",                {"the","python","tutorial"},                     {"python","docs"}),
            mk(0, "https://www.python.org/",                            {"welcome","to","python","org"},                 {"python"}),
            mk(2, "https://realpython.com/python-first-steps/",         {"python","first","steps"},                      {"python","beginners"}),
            mk(2, "https://wiki.python.org/moin/BeginnersGuide",        {"beginners","guide","python","wiki"},           {"beginner","python"}),
            mk(1, "https://en.cppreference.com/w/cpp",                  {"c","reference"},                               {"cpp","stdlib","reference"}),
            mk(1, "https://isocpp.org/",                                {"standard","c"},                                {"c","iso"}),
            mk(2, "https://en.cppreference.com/w/cpp/language/tutorial",{"c","language","tutorial"},                     {"cpp","tutorial"}),
            mk(1, "https://scikit-learn.org/stable/",                   {"scikit","learn","machine","learning","in","python"}, {"sklearn","machine","learning"}),
            mk(1, "https://www.tensorflow.org/tutorials",               {"tensorflow","tutorials"},                      {"tensorflow","tutorial","ml"}),
            mk(1, "https://pytorch.org/tutorials/",                     {"welcome","to","pytorch","tutorials"},          {"pytorch","tutorial"}),
            mk(2, "https://en.wikipedia.org/wiki/Machine_learning",     {"machine","learning","wikipedia"},              {"ml","wiki"}),
            mk(3, "https://arxiv.org/abs/1706.03762",                   {"attention","is","all","you","need"},           {"transformer","paper","attention"}),
            mk(5, "https://example.tk/get-rich-python-tutorial-FREE",   {"free","python","tutorial","click","now"},      {}),
            mk(6, "http://192.168.0.1/python/tutorial.html",            {"python","tutorial"},                           {}),
            mk(2, "https://stackoverflow.com/questions/tagged/python",  {"newest","python","questions","stack","overflow"}, {"python","stackoverflow"}),
        };
    }

    // try to load per-rank index blobs from the data directory
    int load_rank_blobs(const std::string& data_dir) {
        for (int rank = 0; rank < static_cast<int>(NUM_PAGE_FILE_RANKS); rank++) {
            std::string path = data_dir + "/index_rank_" + std::to_string(rank) + ".blob";
            if (!std::filesystem::exists(path)) continue;
            auto idx = std::make_unique<Index>();
            if (!idx->LoadBlob(path)) {
                std::fprintf(stderr, "[engine] failed to load %s\n", path.c_str());
                continue;
            }
            std::fprintf(stderr, "[engine] loaded %s (%d docs)\n",
                         path.c_str(), idx->GetDocumentCount());
            indexes.push_back(std::move(idx));
        }
        return static_cast<int>(indexes.size());
    }

    void load_demo_corpus() {
        auto idx = std::make_unique<Index>();
        for (const auto& page : make_demo_corpus())
            idx->addDocument(page);
        idx->Finalize();
        std::fprintf(stderr, "[engine] loaded demo corpus (%d docs)\n",
                     idx->GetDocumentCount());
        indexes.push_back(std::move(idx));
    }

    // cascading search across rank buckets
    std::vector<SearchResult> search_local(const std::vector<std::string>& terms) {
        std::vector<SearchResult> results;

        for (size_t r = 0; r < indexes.size(); r++) {
            ConstraintSolver solver(indexes[r].get());
            auto doc_ids = solver.FindAndQuery(terms);

            for (int doc_id : doc_ids) {
                auto meta = indexes[r]->GetDocumentMetadata(doc_id);
                DocCandidate cand = to_candidate(meta);
                double s = compute_score(terms, cand);
                if (s <= 0.0) continue;

                // static and dynamic are baked into the combined score
                // via multiplicative blend. expose them separately for
                // the frontend debug view
                RankerInput in;
                in.url = cand.url;
                in.is_https = (cand.url.find("https://") == 0);
                in.pages_per_domain = 0;
                in.hop_distance = static_cast<size_t>(std::max(cand.hop_distance, 0));
                in.word_count = 0;
                in.content_to_html_ratio = 0.0;
                double ss = StaticRanker(in).rank();
                double ds = score_dynamic(terms, cand, GENERAL);

                results.push_back({doc_id, meta.url, ss, ds, s});
            }

            // cascade: stop descending ranks once we have enough
            if (static_cast<int>(results.size()) >= top_k) break;
        }

        // sort by rank bucket (implicit from order), then score
        std::sort(results.begin(), results.end(),
            [](const SearchResult& a, const SearchResult& b) {
                return a.combined_score > b.combined_score;
            });

        if (static_cast<int>(results.size()) > top_k)
            results.resize(top_k);

        return results;
    }

    std::vector<SearchResult> search_distributed(const std::vector<std::string>& terms) {
        std::string query_str;
        for (size_t i = 0; i < terms.size(); i++) {
            if (i > 0) query_str += " ";
            query_str += terms[i];
        }
        return distributor->search(query_str);
    }

public:
    // local mode: load blobs from data_dir, fall back to demo corpus
    SearchEngine(const std::string& weights_file,
                 const std::string& data_dir = ".",
                 int k = DEFAULT_TOP_K)
        : weights_path(weights_file), top_k(k) {

        if (!load_and_apply_weights(weights_path)) {
            std::cerr << "[engine] warning: could not load " << weights_path
                      << ", all weights are zero" << std::endl;
        }

        if (load_rank_blobs(data_dir) == 0) {
            std::cerr << "[engine] no index blobs found in " << data_dir
                      << ", using demo corpus" << std::endl;
            load_demo_corpus();
        }

        int total = 0;
        for (const auto& idx : indexes) total += idx->GetDocumentCount();
        std::fprintf(stderr, "[engine] ready: %zu rank(s), %d docs\n",
                     indexes.size(), total);
    }

    // distributed mode
    SearchEngine(const std::string& weights_file,
                 const std::string& data_dir,
                 const std::string& shards_config,
                 int k = DEFAULT_TOP_K)
        : weights_path(weights_file), top_k(k) {

        if (!load_and_apply_weights(weights_path)) {
            std::cerr << "[engine] warning: could not load " << weights_path << std::endl;
        }

        distributor = new QueryDistributor(shards_config);

        if (!distributor->has_shards()) {
            std::cerr << "[engine] no shards configured, falling back to local" << std::endl;
            delete distributor;
            distributor = nullptr;

            if (load_rank_blobs(data_dir) == 0) load_demo_corpus();
        } else {
            std::cerr << "[engine] running in distributed mode" << std::endl;
        }
    }

    ~SearchEngine() {
        delete distributor;
    }

    // main entry point. takes raw user query, compiles it, searches.
    std::vector<SearchResult> search(const std::string& raw_query) {
        auto terms = query::compile(raw_query);
        if (terms.empty()) return {};

        load_and_apply_weights(weights_path);

        if (distributor) return search_distributed(terms);
        return search_local(terms);
    }
};
