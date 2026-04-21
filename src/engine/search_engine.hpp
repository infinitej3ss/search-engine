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
#include "../index/index_builder.h"
#include "../index/page_data.h"
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
    std::string data_dir;  // directory holding blobs + crawled page files

    // distributed mode
    QueryDistributor* distributor = nullptr;

    static DocCandidate to_candidate(const Index::DocumentMetadata& meta) {
        DocCandidate d;
        d.doc_id = static_cast<uint32_t>(meta.doc_id);
        d.url = meta.url;
        d.title_words = meta.title_words;
        d.hop_distance = meta.hop_distance;
        d.body_length = meta.body_length;
        // body_tf / body_positions are filled in per-query by the caller
        return d;
    }

    static std::string ensure_trailing_slash(const std::string& p) {
        if (p.empty() || p.back() == '/') return p;
        return p + "/";
    }

    // build a snippet by fetching the raw page from its crawler file and
    // centering a window around the first query-term hit. called per result
    // on the paginated page only, not per candidate
    std::string fetch_snippet(const std::vector<std::string>& terms,
                              const Index::DocumentMetadata& meta) const {
        if (terms.empty() || data_dir.empty()) return "";

        PageData pd;
        if (get_page_data_from_index(pd, data_dir,
                                     meta.page_file_rank,
                                     meta.page_file_num,
                                     meta.page_file_index) != 0) {
            return "";
        }
        if (pd.words.empty()) return "";

        constexpr int WINDOW = 20;
        constexpr size_t MAX_WORD_LEN = 30;
        constexpr size_t MAX_SNIPPET_LEN = 200;

        auto is_clean = [](const std::string& w) {
            return w.size() <= 40 && w.find('{') == std::string::npos
                                  && w.find('<') == std::string::npos;
        };

        // find the first clean occurrence of any query term (case-insensitive)
        int best_pos = -1;
        for (size_t j = 0; j < pd.words.size() && best_pos < 0; j++) {
            if (!is_clean(pd.words[j])) continue;
            std::string lower;
            lower.reserve(pd.words[j].size());
            for (char c : pd.words[j])
                lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (const auto& t : terms) {
                if (lower == t) { best_pos = static_cast<int>(j); break; }
            }
        }
        if (best_pos < 0) best_pos = 0;

        int start = std::max(0, best_pos - WINDOW / 2);
        int end = std::min(static_cast<int>(pd.words.size()), start + WINDOW);

        std::string snippet;
        if (start > 0) snippet += "... ";
        for (int i = start; i < end; i++) {
            if (!is_clean(pd.words[i])) continue;
            if (!snippet.empty() && snippet.back() != ' ') snippet += " ";
            const auto& w = pd.words[i];
            if (w.size() > MAX_WORD_LEN) snippet += w.substr(0, MAX_WORD_LEN) + "...";
            else                         snippet += w;
            if (snippet.size() >= MAX_SNIPPET_LEN) break;
        }
        if (end < static_cast<int>(pd.words.size())) snippet += " ...";

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

    static const WeightProfile& select_profile(
        const std::vector<std::string>& terms) {
        return terms.size() == 1 ? NAVIGATIONAL : GENERAL;
    }

    static double compute_score(const std::vector<std::string>& terms,
                                 const DocCandidate& cand,
                                 const WeightProfile& profile,
                                 double bm25_norm = 0.0) {
        RankerInput in;
        in.url = cand.url;
        in.is_https = (cand.url.find("https://") == 0);
        in.pages_per_domain = 0;
        in.hop_distance = static_cast<size_t>(std::max(cand.hop_distance, 0));
        in.word_count = 0;
        in.content_to_html_ratio = 0.0;
        in.title = join_title(cand.title_words);

        double s = StaticRanker(in).rank();
        if (s < 0.0) return -1.0;

        // floor static score so distant/low-quality pages aren't
        // zeroed out when they're genuinely relevant
        s = std::max(s, STATIC_FLOOR);

        double d = score_dynamic(terms, cand, profile, bm25_norm);

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
                total += idx->GetBodyLength(i);
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
        // build the demo index in-memory, write to a tmp V4 blob, then
        // mmap-load it. that keeps the query path uniform (always mmap)
        // without needing a separate "in-memory" Index mode
        IndexBuilder b;
        for (const auto& page : make_demo_corpus()) b.addDocument(page);
        b.Finalize();

        std::string tmp = data_dir + "demo_corpus.blob";
        if (!b.WriteBlobV4(tmp)) {
            std::fprintf(stderr, "[engine] failed to write demo corpus blob\n");
            return;
        }
        auto idx = std::make_unique<Index>();
        if (!idx->LoadBlob(tmp)) {
            std::fprintf(stderr, "[engine] failed to mmap demo corpus blob\n");
            return;
        }
        std::fprintf(stderr, "[engine] loaded demo corpus (%d docs)\n",
                     idx->GetDocumentCount());
        indexes.push_back(std::move(idx));
    }

public:
    struct DocMatch {
        int doc_id;
        SpanResult span;
    };

    // walk an ISR tree, collecting unique doc ids and their span info
    static std::vector<DocMatch> walk_isr_docs(ISR* root) {
        std::vector<DocMatch> matches;
        if (!root || !root->IsValid()) return matches;
        while (root->IsValid()) {
            int d = root->GetCurrentDocId();
            if (d < 0) break;
            if (matches.empty() || matches.back().doc_id != d) {
                matches.push_back({d, root->GetMatchSpan()});
            }
            if (!root->SkipToDoc(d + 1)) break;
        }
        return matches;
    }

    // search a single rank/level. compiles the AST into an ISR tree for
    // this level's index. returns results sorted by combined score desc
    std::vector<SearchResult> search_level(const query::QueryNode* ast,
                                            const std::vector<std::string>& terms,
                                            size_t level,
                                            SearchStats* stats = nullptr,
                                            const WeightProfile* profile = nullptr) {
        std::vector<SearchResult> results;
        if (level >= indexes.size() || !ast) return results;

        auto root = query::compile_to_isr(*ast, indexes[level].get());
        auto matches = walk_isr_docs(root.get());

        if (stats) stats->constraint_solved += static_cast<int>(matches.size());

        int n_docs = indexes[level]->GetDocumentCount();
        double avg_len = (level < avg_doc_lengths.size()) ? avg_doc_lengths[level] : 500.0;
        if (avg_len <= 0.0) avg_len = 500.0;
        BM25 bm25(n_docs, avg_len);

        std::unordered_map<std::string, int> doc_freq;
        for (const auto& term : terms) {
            doc_freq[term] = indexes[level]->GetDocumentFrequency(term);
        }

        struct Candidate {
            int doc_id;
            Index::DocumentMetadata meta;
            DocCandidate cand;
            double bm25_raw;
        };
        std::vector<Candidate> candidates;

        for (const auto& m : matches) {
            auto meta = indexes[level]->GetDocumentMetadata(m.doc_id);
            DocCandidate cand = to_candidate(meta);

            // isr-derived body span
            cand.body_span = m.span;
            cand.has_body_span = true;

            // per-query signals for T1 (body_tf) and BM25
            for (const auto& term : terms) {
                cand.body_tf[term] =
                    indexes[level]->GetFieldTermFrequency(m.doc_id, term, 'b');
            }
            double bm25_raw = bm25.score(cand.body_length, cand.body_tf, terms, doc_freq);

            candidates.push_back({m.doc_id, std::move(meta), std::move(cand), bm25_raw});
        }

        // sigmoid normalization centered on median
        double median_bm25 = 0.0;
        if (!candidates.empty()) {
            std::vector<double> bm25_vals;
            bm25_vals.reserve(candidates.size());
            for (const auto& c : candidates) bm25_vals.push_back(c.bm25_raw);
            std::sort(bm25_vals.begin(), bm25_vals.end());
            median_bm25 = bm25_vals[bm25_vals.size() / 2];
        }

        const WeightProfile& wp = profile ? *profile : select_profile(terms);

        for (auto& c : candidates) {
            double bm25_norm = 1.0 / (1.0 + std::exp(-SIGMOID_K * (c.bm25_raw - median_bm25)));
            double s = compute_score(terms, c.cand, wp, bm25_norm);
            if (s <= 0.0) continue;

            RankerInput in;
            in.url = c.cand.url;
            in.is_https = (c.cand.url.find("https://") == 0);
            in.pages_per_domain = 0;
            in.hop_distance = static_cast<size_t>(std::max(c.cand.hop_distance, 0));
            in.word_count = 0;
            in.content_to_html_ratio = 0.0;
            double ss = StaticRanker(in).rank();
            double ds = score_dynamic(terms, c.cand, wp, bm25_norm);

            std::string title = join_title(c.meta.title_words);
            // snippet deferred to post-pagination (see SearchEngine::search)
            SearchResult sr{c.doc_id, c.meta.url, title, "", ss, ds, s};
            // per-signal breakdown (cheap: these are already-computed primitives)
            sr.t1 = t1_metastream(terms, c.cand);
            sr.t2 = t2_span(terms, c.cand);
            sr.t3 = t3_quality(c.cand);
            sr.bm25 = bm25_norm;
            sr.t5 = t5_title_coverage(terms, c.cand);
            results.push_back(std::move(sr));

            if (stats) {
                stats->passed_static_floor++;
                if (level < stats->per_rank_matched.size()) {
                    stats->per_rank_matched[level]++;
                }
            }
        }

        std::sort(results.begin(), results.end(),
            [](const SearchResult& a, const SearchResult& b) {
                return a.combined_score > b.combined_score;
            });

        return results;
    }

    size_t num_levels() const { return indexes.size(); }

    // fetch a snippet for a single doc_id (phase-2 distributed snippets).
    // searches all levels for the doc_id since the caller may not know the
    // rank. returns empty string if the doc can't be found or has no page data
    std::string fetch_snippet_by_id(int doc_id,
                                     const std::vector<std::string>& terms) const {
        for (const auto& idx : indexes) {
            if (doc_id < 0 || doc_id >= idx->GetDocumentCount()) continue;
            auto meta = idx->GetDocumentMetadata(doc_id);
            if (meta.url.empty()) continue;
            auto s = fetch_snippet(terms, meta);
            if (!s.empty()) return s;
        }
        return "";
    }

private:
    std::vector<SearchResult> search_distributed(const std::string& raw_query,
                                                  SearchStats* stats = nullptr,
                                                  const std::string& weights_param = "") {
        return distributor->search(raw_query, stats, weights_param);
    }

public:
    // local mode: load blobs from data_dir, fall back to demo corpus
    SearchEngine(const std::string& weights_file,
                 const std::string& data_dir_ = ".")
        : weights_path(weights_file), data_dir(ensure_trailing_slash(data_dir_)) {

        if (!load_and_apply_weights(weights_path)) {
            std::cerr << "[engine] warning: could not load " << weights_path
                      << ", all weights are zero" << std::endl;
        }

        if (load_rank_blobs(data_dir_) == 0) {
            std::cerr << "[engine] no index blobs found in " << data_dir_
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
                 const std::string& data_dir_,
                 const std::string& shards_config)
        : weights_path(weights_file), data_dir(ensure_trailing_slash(data_dir_)) {

        if (!load_and_apply_weights(weights_path)) {
            std::cerr << "[engine] warning: could not load " << weights_path << std::endl;
        }

        distributor = new QueryDistributor(shards_config);

        if (!distributor->has_shards()) {
            std::cerr << "[engine] no shards configured, falling back to local" << std::endl;
            delete distributor;
            distributor = nullptr;

            if (load_rank_blobs(data_dir_) == 0) load_demo_corpus();
        } else {
            std::cerr << "[engine] running in distributed mode" << std::endl;
        }
    }

    ~SearchEngine() {
        delete distributor;
    }

    // main entry point. takes raw user query, compiles it, searches
    // offset/limit for pagination. returns the page and total result count.
    // `stats` is optional; if provided it's filled with per-query diagnostics
    // (funnel, per-rank counts, parsed tokens) for the debug widgets
    std::vector<SearchResult> search(const std::string& raw_query,
                                      int offset = 0, int limit = 10,
                                      int* total_out = nullptr,
                                      SearchStats* stats = nullptr,
                                      bool dev_mode = false) {
        auto compiled = query::compile(raw_query);
        auto& terms = compiled.terms;
        if (stats) {
            stats->parsed_tokens = terms;
            if (compiled.ast) stats->parsed_query_ast = compiled.ast->to_string();
        }

        if (compiled.empty()) {
            if (total_out) *total_out = 0;
            return {};
        }

        load_and_apply_weights(weights_path);

        // distributed path: phase-1 fetches results (no snippets), then
        // phase-2 fetches snippets only for the paginated page
        if (distributor) {
            std::string wp = dev_mode ? serialize_weights() : "";
            auto all = search_distributed(raw_query, stats, wp);
            if (total_out) *total_out = static_cast<int>(all.size());
            if (offset >= static_cast<int>(all.size())) return {};
            int end = std::min(offset + limit, static_cast<int>(all.size()));
            std::vector<SearchResult> page(all.begin() + offset,
                                            all.begin() + end);
            distributor->fetch_page_snippets(raw_query, page);
            return page;
        }

        if (stats) stats->per_rank_matched.assign(indexes.size(), 0);

        // local path: collect level-tagged results so we can fetch snippets
        // only for the paginated slice (each snippet is a disk read)
        struct Tagged { SearchResult result; size_t level; };
        std::vector<Tagged> tagged;
        for (size_t r = 0; r < indexes.size(); r++) {
            const auto& wp = select_profile(terms);
            auto level_results = search_level(compiled.ast.get(), terms, r, stats, &wp);
            for (auto& res : level_results) tagged.push_back({std::move(res), r});
        }
        std::sort(tagged.begin(), tagged.end(),
            [](const Tagged& a, const Tagged& b) {
                return a.result.combined_score > b.result.combined_score;
            });

        if (total_out) *total_out = static_cast<int>(tagged.size());
        if (offset >= static_cast<int>(tagged.size())) return {};
        int end = std::min(offset + limit, static_cast<int>(tagged.size()));

        std::vector<SearchResult> page;
        page.reserve(end - offset);
        for (int i = offset; i < end; i++) {
            auto meta = indexes[tagged[i].level]->GetDocumentMetadata(tagged[i].result.doc_id);
            tagged[i].result.snippet = fetch_snippet(terms, meta);
            page.push_back(std::move(tagged[i].result));
        }
        return page;
    }
};
