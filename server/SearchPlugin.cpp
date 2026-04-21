// search plugin for linuxtinyserver
// thin http layer — delegates search logic to src/engine/search_engine.hpp

#include "Plugin.h"

#include <string>
#include <vector>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <chrono>

#include "../src/engine/search_engine.hpp"

// http helpers

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

            return url_decode(value);
        }

        pos = query.find('&', pos);
        if (pos == std::string::npos) break;
        pos++;
    }

    return "";
}

static int get_int_param(const std::string& path, const std::string& key, int fallback) {
    std::string val = get_query_param(path, key);
    if (val.empty()) return fallback;
    try { return std::stoi(val); } catch (...) { return fallback; }
}

// json formatting

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

static std::string results_to_json(const std::string& query,
                                    const std::vector<SearchResult>& results,
                                    int total, int offset, double took_ms,
                                    const SearchStats& stats) {
    std::ostringstream json;
    json << "{\"query\":\"" << json_escape(query) << "\","
         << "\"total\":" << total << ","
         << "\"offset\":" << offset << ","
         << "\"took_ms\":" << took_ms << ",";

    // per-query diagnostics for the sidebar widgets
    json << "\"stats\":{";
    json << "\"parsed_tokens\":[";
    for (size_t i = 0; i < stats.parsed_tokens.size(); i++) {
        if (i > 0) json << ",";
        json << "\"" << json_escape(stats.parsed_tokens[i]) << "\"";
    }
    json << "],";
    json << "\"parsed_query_ast\":\""
         << json_escape(stats.parsed_query_ast) << "\",";
    json << "\"constraint_solved\":" << stats.constraint_solved << ",";
    json << "\"passed_static_floor\":" << stats.passed_static_floor << ",";
    json << "\"per_rank_matched\":[";
    for (size_t i = 0; i < stats.per_rank_matched.size(); i++) {
        if (i > 0) json << ",";
        json << stats.per_rank_matched[i];
    }
    json << "]},";

    json << "\"results\":[";
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) json << ",";
        const auto& r = results[i];
        json << "{\"doc_id\":" << r.doc_id
             << ",\"url\":\"" << json_escape(r.url) << "\""
             << ",\"title\":\"" << json_escape(r.title) << "\""
             << ",\"snippet\":\"" << json_escape(r.snippet) << "\""
             << ",\"static_score\":" << r.static_score
             << ",\"dynamic_score\":" << r.dynamic_score
             << ",\"combined_score\":" << r.combined_score
             << ",\"t1\":" << r.t1
             << ",\"t2\":" << r.t2
             << ",\"t3\":" << r.t3
             << ",\"bm25\":" << r.bm25
             << ",\"t5\":" << r.t5
             << "}";
    }

    json << "]}";
    return json.str();
}

// the plugin

// if LEADER_SHARDS_CONFIG env var is set, run in distributed mode.
// otherwise, run in local mode and serve from LEADER_DATA_DIR (default "data").
static SearchEngine make_engine() {
    const char* shards = std::getenv("LEADER_SHARDS_CONFIG");
    const char* data_dir = std::getenv("LEADER_DATA_DIR");
    std::string dd = data_dir ? data_dir : "data";

    if (shards) {
        return SearchEngine("config/weights.txt", dd, shards);
    }
    return SearchEngine("config/weights.txt", dd);
}

class SearchPlugin : public PluginObject {
private:
    SearchEngine engine;
    bool dev_mode;

public:
    SearchPlugin()
        : engine(make_engine()),
          dev_mode(std::getenv("DEV_MODE") != nullptr) {
        if (dev_mode) {
            std::fprintf(stderr, "[search] dev mode enabled — "
                         "weights will be forwarded to shards\n");
        }
        Plugin = this;
    }

    bool MagicPath(const std::string path) override {
        return path == "/search" || path.find("/search?") == 0;
    }

    std::string ProcessRequest(std::string request) override {
        std::string query_str = get_query_param(request, "q");
        int offset = get_int_param(request, "offset", 0);
        int limit = get_int_param(request, "limit", 10);

        int total = 0;
        SearchStats stats;
        auto start = std::chrono::steady_clock::now();
        auto results = engine.search(query_str, offset, limit, &total, &stats,
                                     dev_mode);
        double took_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();

        std::fprintf(stderr, "[search] q=%.80s offset=%d total=%d took=%.1fms\n",
                     query_str.c_str(), offset, total, took_ms);

        return results_to_json(query_str, results, total, offset, took_ms, stats);
    }
};

static SearchPlugin searchPluginInstance;
