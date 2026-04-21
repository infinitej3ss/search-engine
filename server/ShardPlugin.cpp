// shard-side plugin for linuxtinyserver.
// handles /query?q=... requests by running search over this shard's data
// and streaming results back to the leader as newline-delimited json.
//
// iterates level-by-level (highest quality rank first). between levels,
// flushes results to the leader. stops early if:
//   - leader closes the socket (send returns error)
//   - shard has emitted enough good-quality results
//   - all levels exhausted
//
// configuration: SHARD_DATA_DIR env var points at this shard's blob dir.
// shard.good_threshold, shard.good_count, shard.max_results in weights.txt
// control early-termination behavior.

#include "Plugin.h"

#include <string>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <sys/socket.h>

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

static std::string result_to_line(const SearchResult& r) {
    std::ostringstream json;
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
         << "}\n";
    return json.str();
}

// emit one additional ndjson line at the end of the stream carrying this
// shard's SearchStats. leader distinguishes it by the "_type":"stats" marker
static std::string stats_to_line(const SearchStats& s) {
    std::ostringstream json;
    json << "{\"_type\":\"stats\""
         << ",\"constraint_solved\":" << s.constraint_solved
         << ",\"passed_static_floor\":" << s.passed_static_floor
         << ",\"per_rank_matched\":[";
    for (size_t i = 0; i < s.per_rank_matched.size(); i++) {
        if (i > 0) json << ",";
        json << s.per_rank_matched[i];
    }
    json << "]}\n";
    return json.str();
}

// write all bytes or fail. uses MSG_NOSIGNAL so a closed peer gives us
// EPIPE instead of SIGPIPE killing the process.
// https://stackoverflow.com/questions/108183/how-to-prevent-sigpipes-or-handle-them-properly
static bool write_all(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

// the shard plugin

class ShardPlugin : public PluginObject {
private:
    SearchEngine engine;

    static std::string data_dir_from_env() {
        const char* dir = std::getenv("SHARD_DATA_DIR");
        return dir ? std::string(dir) : std::string("data");
    }

public:
    ShardPlugin()
        : engine("config/weights.txt", data_dir_from_env()) {
        Plugin = this;
    }

    bool MagicPath(const std::string path) override {
        return path == "/query" || path.find("/query?") == 0
            || path == "/snippet" || path.find("/snippet?") == 0;
    }

    bool StreamingResponse(const std::string path) override {
        return MagicPath(path);
    }

    std::string ProcessRequest(std::string /*request*/) override {
        return "";
    }

    // parse comma-separated doc_ids param
    static std::vector<int> parse_doc_ids(const std::string& raw) {
        std::vector<int> out;
        size_t i = 0;
        while (i < raw.size()) {
            size_t comma = raw.find(',', i);
            std::string tok = raw.substr(i, comma == std::string::npos
                                                ? std::string::npos : comma - i);
            if (!tok.empty()) {
                try { out.push_back(std::stoi(tok)); } catch (...) {}
            }
            if (comma == std::string::npos) break;
            i = comma + 1;
        }
        return out;
    }

    void ProcessSnippetRequest(const std::string& request, int talkFD) {
        std::string query_str = get_query_param(request, "q");
        std::string doc_ids_raw = get_query_param(request, "doc_ids");
        auto doc_ids = parse_doc_ids(doc_ids_raw);

        std::string header = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: application/x-ndjson\r\n"
                             "Connection: close\r\n\r\n";
        if (!write_all(talkFD, header)) return;

        auto compiled = query::compile(query_str);
        if (compiled.empty()) return;

        for (int doc_id : doc_ids) {
            auto snippet = engine.fetch_snippet_by_id(doc_id, compiled.terms);
            std::ostringstream json;
            json << "{\"doc_id\":" << doc_id
                 << ",\"snippet\":\"" << json_escape(snippet) << "\"}\n";
            if (!write_all(talkFD, json.str())) return;
        }
    }

    void ProcessStreamingRequest(std::string request, int talkFD) override {
        if (request.find("/snippet") != std::string::npos) {
            ProcessSnippetRequest(request, talkFD);
            return;
        }

        std::string query_str = get_query_param(request, "q");
        std::string weights_str = get_query_param(request, "w");

        // dev mode: leader sent weights — apply them for this query
        if (!weights_str.empty()) {
            apply_weights_from_string(weights_str);
        }

        std::string header = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: application/x-ndjson\r\n"
                             "Connection: close\r\n\r\n";
        if (!write_all(talkFD, header)) return;

        auto compiled = query::compile(query_str);
        auto& terms = compiled.terms;
        if (compiled.empty()) {
            std::cerr << "[shard] empty query after compile" << std::endl;
            return;
        }

        int total_sent = 0;
        int good_sent = 0;

        SearchStats stats;
        stats.parsed_tokens = terms;
        if (compiled.ast) stats.parsed_query_ast = compiled.ast->to_string();
        stats.per_rank_matched.assign(engine.num_levels(), 0);

        // iterate levels from highest quality (0) to lowest.
        // after each level, check whether we've hit the good-results threshold.
        // if the leader disconnected mid-stream, bail immediately
        for (size_t level = 0; level < engine.num_levels(); level++) {
            auto level_results = engine.search_level(compiled.ast.get(), terms, level, &stats);

            int level_sent = 0;
            for (const auto& r : level_results) {
                if (level_sent >= SHARD_MAX_RESULTS) break;

                if (!write_all(talkFD, result_to_line(r))) {
                    std::cerr << "[shard] leader disconnected mid-level "
                              << level << " after " << total_sent
                              << " total results" << std::endl;
                    return;
                }
                total_sent++;
                level_sent++;
                if (r.combined_score >= SHARD_GOOD_THRESHOLD) {
                    good_sent++;
                }
            }

            std::cerr << "[shard] level " << level << ": emitted "
                      << level_sent << " results (" << good_sent
                      << " good total)" << std::endl;

            if (good_sent >= SHARD_GOOD_COUNT) {
                std::cerr << "[shard] good threshold reached after level "
                          << level << ", skipping lower levels" << std::endl;
                break;
            }
        }

        // best-effort: emit stats. if leader already closed the socket,
        // write_all returns false and we just let it drop
        write_all(talkFD, stats_to_line(stats));

        std::cerr << "[shard] done: " << total_sent << " results for \""
                  << query_str << "\"" << std::endl;
    }
};

static ShardPlugin shardPluginInstance;
