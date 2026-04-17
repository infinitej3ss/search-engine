#pragma once

// distributes a query to all index shards in parallel, collects results,
// and merge-sorts by combined_score.

#include <vector>
#include <string>
#include <algorithm>
#include <thread>
#include <mutex>
#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>

#include "shard_client.hpp"
#include "search_result.hpp"

class QueryDistributor {
private:
    std::vector<ShardClient> shards;
    int global_timeout_ms;
    int shard_timeout_ms;
    int results_needed;

    // per-shard query function, called from its own thread.
    // appends results to shared vector under mutex.
    static void query_shard(ShardClient* client,
                            const std::string& query_str,
                            int timeout_ms,
                            std::vector<SearchResult>* all_results,
                            std::mutex* mtx) {
        auto results = client->query(query_str, timeout_ms);

        std::lock_guard<std::mutex> lock(*mtx);
        all_results->insert(all_results->end(), results.begin(), results.end());
    }

public:
    // config file format (one shard per line):
    //   host:port
    //   # comments and blank lines are ignored
    //   timeout_ms 1000
    //   shard_timeout_ms 400
    //   results_needed 10
    QueryDistributor(const std::string& config_path)
        : global_timeout_ms(1000), shard_timeout_ms(600), results_needed(10) {

        std::ifstream in(config_path);
        if (!in.is_open()) {
            std::cerr << "[distributor] could not open " << config_path << std::endl;
            return;
        }

        std::string line;
        while (std::getline(in, line)) {
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos || line[start] == '#') continue;

            std::istringstream iss(line);
            std::string token;
            iss >> token;

            // config directives
            if (token == "timeout_ms") {
                iss >> global_timeout_ms;
            } else if (token == "shard_timeout_ms") {
                iss >> shard_timeout_ms;
            } else if (token == "results_needed") {
                iss >> results_needed;
            } else {
                // host:port
                size_t colon = token.find(':');
                if (colon != std::string::npos) {
                    std::string host = token.substr(0, colon);
                    int port = std::stoi(token.substr(colon + 1));
                    shards.emplace_back(ShardConfig{host, port});
                }
            }
        }

        std::cerr << "[distributor] " << shards.size() << " shards, "
                  << "global timeout " << global_timeout_ms << "ms, "
                  << "shard timeout " << shard_timeout_ms << "ms"
                  << std::endl;
    }

    bool has_shards() const { return !shards.empty(); }

    // fan out query to all shards, collect results, merge-sort by score.
    std::vector<SearchResult> search(const std::string& query_str) {
        std::vector<SearchResult> all_results;
        std::mutex mtx;

        // launch one thread per shard
        std::vector<std::thread> threads;
        for (auto& shard : shards) {
            threads.emplace_back(query_shard, &shard, query_str,
                                 shard_timeout_ms, &all_results, &mtx);
        }

        // wait up to global timeout for threads to finish
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(global_timeout_ms);

        for (auto& t : threads) {
            auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining.count() > 0) {
                // can't do timed join with std::thread — just join and let
                // the shard's own timeout handle the cutoff.
                // the global timeout is enforced by giving shards a shorter
                // timeout than the leader's.
                t.join();
            } else {
                // past deadline, but we still need to join (threads will
                // finish soon since their timeout is shorter)
                t.join();
            }
        }

        std::cerr << "[distributor] collected " << all_results.size()
                  << " results from " << shards.size() << " shards"
                  << std::endl;

        // merge-sort by combined score, descending
        std::sort(all_results.begin(), all_results.end(),
            [](const SearchResult& a, const SearchResult& b) {
                return a.combined_score > b.combined_score;
            });

        // trim to requested count
        if (all_results.size() > static_cast<size_t>(results_needed)) {
            all_results.resize(results_needed);
        }

        return all_results;
    }
};
