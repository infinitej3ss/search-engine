#pragma once

// core search logic. two modes:
// - local: loads index blobs (or demo corpus) in-process
// - distributed: fans out to shards via query_distributor

#include <algorithm>
#include <cmath>
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
#include "../ranker/dynamic/bm25.hpp"

class SearchEngine {
private:
    // local mode, one index per rank bucket
    std::vector<std::unique_ptr<Index>> indexes;
    std::vector<double> avg_doc_lengths; // per-index average body word count
    std::string weights_path;

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

    // build a snippet by finding the first query term in body_words
    // and extracting a window around it. skips junk tokens (json, markup).
    static std::string make_snippet(const std::vector<std::string>& terms,
                                     const std::vector<std::string>& body_words,
                                     int window = 20) {
        if (body_words.empty() || terms.empty()) return "";

        constexpr size_t MAX_WORD_LEN = 30;
        constexpr size_t MAX_SNIPPET_LEN = 200;

        // skip words that look like markup/json
        auto is_clean = [](const std::string& w) {
            return w.size() <= 40 && w.find('{') == std::string::npos
                                  && w.find('<') == std::string::npos;
        };

        // find the position of the first matching clean term
        int best_pos = -1;
        for (size_t j = 0; j < body_words.size() && best_pos < 0; j++) {
            if (!is_clean(body_words[j])) continue;
            std::string lower;
            for (char c : body_words[j])
                lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (const auto& t : terms) {
                if (lower == t) { best_pos = static_cast<int>(j); break; }
            }
        }

        if (best_pos < 0) best_pos = 0;

        int start = std::max(0, best_pos - window / 2);
        int end = std::min(static_cast<int>(body_words.size()), start + window);

        std::string snippet;
        if (start > 0) snippet += "... ";
        for (int i = start; i < end; i++) {
            if (!is_clean(body_words[i])) continue;
            if (!snippet.empty() && snippet.back() != ' ') snippet += " ";
            const auto& w = body_words[i];
            if (w.size() > MAX_WORD_LEN)
                snippet += w.substr(0, MAX_WORD_LEN) + "...";
            else
                snippet += w;
            if (snippet.size() >= MAX_SNIPPET_LEN) break;
        }
        if (end < static_cast<int>(body_words.size())) snippet += " ...";

        return decode_html_entities(snippet);
    }

    // decode common html entities in a string
    static std::string decode_html_entities(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '&') {
                // named entities
                if (s.compare(i, 5, "&amp;") == 0) { out += '&'; i += 4; }
                else if (s.compare(i, 4, "&lt;") == 0) { out += '<'; i += 3; }
                else if (s.compare(i, 4, "&gt;") == 0) { out += '>'; i += 3; }
                else if (s.compare(i, 6, "&quot;") == 0) { out += '"'; i += 5; }
                else if (s.compare(i, 6, "&apos;") == 0) { out += '\''; i += 5; }
                // numeric entities: &#NNN; or &#xHHH;
                else if (i + 2 < s.size() && s[i + 1] == '#') {
                    size_t end = s.find(';', i + 2);
                    if (end != std::string::npos && end - i < 10) {
                        int code = 0;
                        if (s[i + 2] == 'x' || s[i + 2] == 'X')
                            code = std::stoi(s.substr(i + 3, end - i - 3), nullptr, 16);
                        else
                            code = std::stoi(s.substr(i + 2, end - i - 2));
                        if (code > 0 && code < 128) out += static_cast<char>(code);
                        i = end;
                    } else {
                        out += s[i];
                    }
                } else {
                    out += s[i];
                }
            } else {
                out += s[i];
            }
        }
        return out;
    }

    // join title words back into a string, decoding html entities
    static std::string join_title(const std::vector<std::string>& words) {
        std::string out;
        for (size_t i = 0; i < words.size(); i++) {
            if (i > 0) out += " ";
            out += words[i];
        }
        return decode_html_entities(out);
    }

    static double compute_score(const std::vector<std::string>& terms,
                                 const DocCandidate& cand,
                                 double bm25_norm = 0.0) {
        RankerInput in;
        in.url = cand.url;
        in.is_https = (cand.url.find("https://") == 0);
        in.pages_per_domain = 0;
        in.hop_distance = static_cast<size_t>(std::max(cand.hop_distance, 0));
        in.word_count = 0;
        in.content_to_html_ratio = 0.0;

        double s = StaticRanker(in).rank();
        if (s < 0.0) return -1.0;

        // floor static score so distant/low-quality pages aren't
        // zeroed out when they're genuinely relevant
        s = std::max(s, STATIC_FLOOR);

        double d = score_dynamic(terms, cand, GENERAL, bm25_norm);

        // power-weighted combination: static^α × dynamic^(1-α)
        // α < 0.5 means dynamic (relevance) dominates over static (quality).
        // tunable via combine_alpha in config/weights.txt
        return std::pow(s, COMBINE_ALPHA) * std::pow(d, 1.0 - COMBINE_ALPHA);
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

    // compute average body word count per index for bm25 normalization
    void compute_avg_doc_lengths() {
        avg_doc_lengths.clear();
        for (const auto& idx : indexes) {
            int n = idx->GetDocumentCount();
            if (n == 0) { avg_doc_lengths.push_back(0.0); continue; }

            double total = 0.0;
            for (int i = 0; i < n; i++) {
                total += idx->GetDocumentMetadata(i).body_words.size();
            }
            avg_doc_lengths.push_back(total / n);
            std::fprintf(stderr, "[engine] rank %zu: avg doc length %.1f words\n",
                         avg_doc_lengths.size() - 1, avg_doc_lengths.back());
        }
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

            // compute average doc length for bm25 normalization
            int n_docs = indexes[r]->GetDocumentCount();
            double avg_len = 0.0;
            if (!avg_doc_lengths.empty() && r < avg_doc_lengths.size()) {
                avg_len = avg_doc_lengths[r];
            }
            BM25 bm25(n_docs, avg_len > 0.0 ? avg_len : 500.0);

            // build doc frequency map for query terms
            std::unordered_map<std::string, int> doc_freq;
            for (const auto& term : terms) {
                doc_freq[term] = indexes[r]->GetDocumentFrequency(term);
            }

            // score all candidates, track max bm25 for normalization
            struct Candidate {
                int doc_id;
                Index::DocumentMetadata meta;
                DocCandidate cand;
                double bm25_raw;
            };
            std::vector<Candidate> candidates;
            double max_bm25 = 0.0;

            for (int doc_id : doc_ids) {
                auto meta = indexes[r]->GetDocumentMetadata(doc_id);
                DocCandidate cand = to_candidate(meta);

                double bm25_raw = bm25.score(cand.body_words, terms, doc_freq);
                if (bm25_raw > max_bm25) max_bm25 = bm25_raw;

                candidates.push_back({doc_id, std::move(meta), std::move(cand), bm25_raw});
            }

            // sigmoid normalization for bm25: avoids per-query min-max
            // inflation. compute median as the centering point.
            double median_bm25 = 0.0;
            if (!candidates.empty()) {
                std::vector<double> bm25_vals;
                bm25_vals.reserve(candidates.size());
                for (const auto& c : candidates) bm25_vals.push_back(c.bm25_raw);
                std::sort(bm25_vals.begin(), bm25_vals.end());
                median_bm25 = bm25_vals[bm25_vals.size() / 2];
            }
            // k controls sigmoid steepness — 0.1 gives a gentle curve
            for (auto& c : candidates) {
                double bm25_norm = 1.0 / (1.0 + std::exp(-SIGMOID_K * (c.bm25_raw - median_bm25)));
                double s = compute_score(terms, c.cand, bm25_norm);
                if (s <= 0.0) continue;

                RankerInput in;
                in.url = c.cand.url;
                in.is_https = (c.cand.url.find("https://") == 0);
                in.pages_per_domain = 0;
                in.hop_distance = static_cast<size_t>(std::max(c.cand.hop_distance, 0));
                in.word_count = 0;
                in.content_to_html_ratio = 0.0;
                double ss = StaticRanker(in).rank();
                double ds = score_dynamic(terms, c.cand, GENERAL, bm25_norm);

                std::string title = join_title(c.meta.title_words);
                std::string snippet = make_snippet(terms, c.cand.body_words);

                results.push_back({c.doc_id, c.meta.url, title, snippet, ss, ds, s});
            }
        }

        // sort by rank bucket (implicit from order), then score
        std::sort(results.begin(), results.end(),
            [](const SearchResult& a, const SearchResult& b) {
                return a.combined_score > b.combined_score;
            });

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
                 const std::string& data_dir = ".")
        : weights_path(weights_file) {

        if (!load_and_apply_weights(weights_path)) {
            std::cerr << "[engine] warning: could not load " << weights_path
                      << ", all weights are zero" << std::endl;
        }

        if (load_rank_blobs(data_dir) == 0) {
            std::cerr << "[engine] no index blobs found in " << data_dir
                      << ", using demo corpus" << std::endl;
            load_demo_corpus();
        }

        compute_avg_doc_lengths();

        int total = 0;
        for (const auto& idx : indexes) total += idx->GetDocumentCount();
        std::fprintf(stderr, "[engine] ready: %zu rank(s), %d docs\n",
                     indexes.size(), total);
    }

    // distributed mode
    SearchEngine(const std::string& weights_file,
                 const std::string& data_dir,
                 const std::string& shards_config)
        : weights_path(weights_file) {

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
    // offset/limit for pagination. returns the page and total result count.
    std::vector<SearchResult> search(const std::string& raw_query,
                                      int offset = 0, int limit = 10,
                                      int* total_out = nullptr) {
        auto terms = query::compile(raw_query);
        if (terms.empty()) {
            if (total_out) *total_out = 0;
            return {};
        }

        load_and_apply_weights(weights_path);

        auto all = distributor ? search_distributed(terms) : search_local(terms);

        if (total_out) *total_out = static_cast<int>(all.size());

        // slice for pagination
        if (offset >= static_cast<int>(all.size())) return {};
        int end = std::min(offset + limit, static_cast<int>(all.size()));
        return std::vector<SearchResult>(all.begin() + offset, all.begin() + end);
    }
};
