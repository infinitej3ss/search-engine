// search plugin for linuxtinyserver
// thin http layer — delegates search logic to src/engine/search_engine.hpp

#include "Plugin.h"

#include <string>
#include <vector>
#include <sstream>

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
                                    int total, int offset) {
    std::ostringstream json;
    json << "{\"query\":\"" << json_escape(query) << "\","
         << "\"total\":" << total << ","
         << "\"offset\":" << offset << ","
         << "\"results\":[";

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
             << "}";
    }

    json << "]}";
    return json.str();
}

// the plugin

class SearchPlugin : public PluginObject {
private:
    SearchEngine engine;

public:
    SearchPlugin() : engine("config/weights.txt", "data") {
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
        auto results = engine.search(query_str, offset, limit, &total);
        return results_to_json(query_str, results, total, offset);
    }
};

static SearchPlugin searchPluginInstance;
