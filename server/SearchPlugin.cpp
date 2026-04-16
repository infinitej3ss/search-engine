// search plugin for linuxtinyserver
// intercepts /search?q=... requests, queries the index, ranks results,
// and returns json.

#include "Plugin.h"
#include "../index/simple_api.h"

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <cstring>

#include "../src/ranker/static/static_ranker.hpp"
#include "../src/ranker/dynamic/dynamic_ranker.hpp"

// query string parsing

// pulls the value of a query parameter from a path e.g. "/search?q=hello+world"
// return empty string if the key isn't found
// we'll basically just use this with key: q
static std::string get_query_param(const std::string& path, const std::string& key) {
    // query string starts with ?
    size_t qmark = path.find('?');
    if (qmark == std::string::npos) return "";

    std::string query = path.substr(qmark + 1);

    // query is key=value pairs separated by '&'
    std::string target = key + "=";
    size_t pos = 0;

    while (pos < query.size()) {
        // check if this position starts with our key
        if (query.compare(pos, target.size(), target) == 0) {
            size_t value_start = pos + target.size();
            size_t value_end = query.find('&', value_start);
            std::string value = query.substr(value_start,
                value_end == std::string::npos ? std::string::npos : value_end - value_start);

            // '+' in query strings means space
            for (char& c : value) {
                if (c == '+') c = ' ';
            }
            return value;
        }

        // skip to next '&'
        pos = query.find('&', pos);
        if (pos == std::string::npos) break;
        pos++; // skip past the '&'
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

// json helpers

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

// result type

struct SearchResult {
    int doc_id;
    std::string url;
    double static_score;
    double dynamic_score;
    double combined_score;
};

// the plugin

class SearchPlugin : public PluginObject {
private:
    IndexHandle index;

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
    }

    std::string build_json_response(const std::string& query,
                                     const std::vector<SearchResult>& results) {
        std::ostringstream json;
        json << "{\"query\":\"" << json_escape(query) << "\","
             << "\"total\":" << results.size() << ","
             << "\"results\":[";

        for (size_t i = 0; i < results.size(); i++) {
            if (i > 0) json << ",";
            const auto& r = results[i];
            json << "{\"doc_id\":" << r.doc_id
                 << ",\"url\":\"" << json_escape(r.url) << "\""
                 << ",\"static_score\":" << r.static_score
                 << ",\"dynamic_score\":" << r.dynamic_score
                 << ",\"combined_score\":" << r.combined_score
                 << "}";
        }

        json << "]}";
        return json.str();
    }

public:
    SearchPlugin() {
        index = create_index();
        load_dummy_data();

        // register with the server
        Plugin = this;
    }

    ~SearchPlugin() override {
        destroy_index(index);
    }

    bool MagicPath(const std::string path) override {
        // claim any path starting with /search
        return path.find("/search") == 0;
    }

    std::string ProcessRequest(std::string request) override {
        // extract query terms from the path
        std::string query_str = get_query_param(request, "q");
        std::vector<std::string> terms = split_terms(query_str);

        std::vector<SearchResult> results;

        if (!terms.empty()) {
            // build the c-style array the index api expects
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

                // static ranking — score based on url quality signals
                double static_score = 0.0;
                if (url) {
                    RankerInput rinput;
                    rinput.url = url;
                    rinput.word_count = 0;
                    rinput.content_to_html_ratio = 0.0;
                    rinput.is_https = (std::string(url).find("https://") == 0);
                    rinput.pages_per_domain = 0;
                    rinput.hop_distance = 0;

                    StaticRanker ranker(rinput);
                    static_score = ranker.rank();
                }

                // dynamic ranking — score based on query-document match
                DocCandidate candidate;
                candidate.doc_id = static_cast<uint32_t>(doc_id);
                candidate.url = url ? url : "";
                // TODO include other metrics when available
                candidate.hop_distance = -1;

                double dynamic_score = score_dynamic(terms, candidate, GENERAL);

                // combined: weighted blend of static + dynamic
                // TODO consider moving this into the ranker module proper
                // static handles "is this a quality page?"
                // dynamic handles "does this page match the query?"
                double combined = 0.4 * static_score + 0.6 * dynamic_score;

                results.push_back({doc_id, url ? url : "", static_score,
                                   dynamic_score, combined});
            }

            free_results(doc_ids);

            // sort by combined score, descending
            std::sort(results.begin(), results.end(),
                [](const SearchResult& a, const SearchResult& b) {
                    return a.combined_score > b.combined_score;
                });
        }

        // build json response
        std::string body = build_json_response(query_str, results);

        return body;
    }
};

// global instance — constructor runs before main(), registers with server
static SearchPlugin searchPluginInstance;
