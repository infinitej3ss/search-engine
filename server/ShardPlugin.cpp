// shard-side plugin for linuxtinyserver
// handles /query?q=...&timeout_ms=... requests from the leader.
// works through index levels autonomously, streaming results as
// newline-delimited json. stops when it has enough good results
// or the timeout expires.

#include "Plugin.h"
#include "../index/simple_api.h"

#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <iostream>
#include <cstdlib>

#include "../config/weights.hpp"
#include "../src/ranker/static/static_ranker.hpp"
#include "../src/ranker/dynamic/dynamic_ranker.hpp"
#include "../src/engine/search_result.hpp"

// how many results above this score threshold before we stop early
static constexpr double GOOD_ENOUGH_THRESHOLD = 0.3;
static constexpr int GOOD_ENOUGH_COUNT = 10;

// placeholder for when the index supports multiple levels.
// right now there's only one level (the whole index).
static constexpr int NUM_LEVELS = 1;

// http helpers (same as SearchPlugin)

static std::string get_query_param(const std::string& path, const std::string& key) {
    size_t qmark = path.find('?');
    if (qmark == std::string::npos) return "";

    std::string query = path.substr(qmark + 1);
    std::string target = key + "=";
    size_t pos = 0;

    while (pos < query.size()) {
        if (query.compare(pos, target.size(), target) == 0) {
            size_t value_start = pos + target.size();
            size_t value_end = query.find('&', value_start);
            std::string value = query.substr(value_start,
                value_end == std::string::npos ? std::string::npos : value_end - value_start);

            for (char& c : value) {
                if (c == '+') c = ' ';
            }
            return value;
        }

        pos = query.find('&', pos);
        if (pos == std::string::npos) break;
        pos++;
    }

    return "";
}

static std::vector<std::string> split_terms(const std::string& query) {
    std::vector<std::string> terms;
    std::istringstream stream(query);
    std::string word;
    while (stream >> word) {
        terms.push_back(word);
    }
    return terms;
}

static std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

// the shard plugin

class ShardPlugin : public PluginObject {
private:
    IndexHandle index;
    std::string weights_path;

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

    // format one result as a json line
    std::string result_to_json(const SearchResult& r) {
        std::ostringstream json;
        json << "{\"doc_id\":" << r.doc_id
             << ",\"url\":\"" << json_escape(r.url) << "\""
             << ",\"static_score\":" << r.static_score
             << ",\"dynamic_score\":" << r.dynamic_score
             << ",\"combined_score\":" << r.combined_score
             << "}";
        return json.str();
    }

    // query one level of the index. currently just queries the whole
    // index since the api doesn't support levels yet. when it does,
    // this will take a level parameter and call the appropriate api.
    std::vector<SearchResult> query_level(
            const std::vector<std::string>& terms, int /* level */) {
        std::vector<SearchResult> results;

        std::vector<const char*> c_terms;
        for (const auto& t : terms) {
            c_terms.push_back(t.c_str());
        }

        int result_count = 0;
        int* doc_ids = find_and_query(index, c_terms.data(),
            static_cast<int>(c_terms.size()), &result_count);

        for (int i = 0; i < result_count; i++) {
            int doc_id = doc_ids[i];
            const char* url = get_document_url(index, doc_id);

            double ss = compute_static_score(url);
            double ds = compute_dynamic_score(terms, doc_id, url);
            double combined = ss * ds;

            results.push_back({doc_id, url ? url : "", ss, ds, combined});
        }

        free_results(doc_ids);
        return results;
    }

public:
    ShardPlugin() : weights_path("config/weights.txt") {
        index = create_index();

        if (!load_and_apply_weights(weights_path)) {
            std::cerr << "[shard] warning: could not load " << weights_path
                      << std::endl;
        }

        // in production, call build_index_from_crawler(index) here
        // to load this shard's portion of the crawled data
        finalize_index(index);

        std::cerr << "[shard] ready, " << get_document_count(index)
                  << " documents" << std::endl;

        Plugin = this;
    }

    ~ShardPlugin() override {
        destroy_index(index);
    }

    bool MagicPath(const std::string path) override {
        return path == "/query" || path.find("/query?") == 0;
    }

    std::string ProcessRequest(std::string request) override {
        std::string query_str = get_query_param(request, "q");
        std::string timeout_str = get_query_param(request, "timeout_ms");
        int timeout_ms = timeout_str.empty() ? 400 : std::stoi(timeout_str);

        std::vector<std::string> terms = split_terms(query_str);

        load_and_apply_weights(weights_path);

        auto start = std::chrono::steady_clock::now();
        auto deadline = start + std::chrono::milliseconds(timeout_ms);

        std::ostringstream body;
        int good_count = 0;

        // work through levels until we have enough results or time runs out
        for (int level = 0; level < NUM_LEVELS; level++) {
            if (std::chrono::steady_clock::now() >= deadline) break;

            auto level_results = query_level(terms, level);

            for (const auto& r : level_results) {
                body << result_to_json(r) << "\n";
                if (r.combined_score >= GOOD_ENOUGH_THRESHOLD) {
                    good_count++;
                }
            }

            std::cerr << "[shard] level " << level << ": "
                      << level_results.size() << " results, "
                      << good_count << " above threshold" << std::endl;

            // stop early if we have enough good results
            if (good_count >= GOOD_ENOUGH_COUNT) break;
        }

        return body.str();
    }
};

static ShardPlugin shardPluginInstance;
