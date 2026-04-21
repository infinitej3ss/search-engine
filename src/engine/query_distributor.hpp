#pragma once

// distributes a query to all index shards in parallel. shards stream
// results back as ndjson; this class collates them and triggers an early
// close once it has enough good-quality hits.
//
// exit paths:
//   1. enough good results collected (count_over_threshold >= needed)
//   2. global timeout_ms elapsed
//   3. every shard stream finished (all shards exhausted their results)

#include <vector>
#include <string>
#include <algorithm>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "shard_client.hpp"
#include "search_result.hpp"

class QueryDistributor {
private:
    std::vector<ShardClient> shards;
    int global_timeout_ms;
    int shard_timeout_ms;
    int results_needed;
    double good_threshold;

    // per-shard worker runs in its own thread.
    // streams results into the shared accumulator, honoring the stop flag.
    // also merges the shard's trailing stats line into the shared SearchStats
    static void worker(ShardClient* client,
                       int shard_idx,
                       const std::string& query,
                       int shard_timeout_ms,
                       double threshold,
                       std::vector<SearchResult>* out,
                       std::mutex* mtx,
                       std::atomic<int>* good_count,
                       std::atomic<bool>* stop_flag,
                       SearchStats* agg_stats,
                       std::atomic<int>* alive_workers) {
        auto on_result = [&](const SearchResult& r) -> bool {
            if (stop_flag->load()) return false;

            {
                std::lock_guard<std::mutex> lock(*mtx);
                SearchResult tagged = r;
                tagged.shard_idx = shard_idx;
                out->push_back(std::move(tagged));
            }

            if (r.combined_score >= threshold) {
                good_count->fetch_add(1);
            }
            return !stop_flag->load();
        };

        auto on_stats = [&](const SearchStats& s) {
            if (!agg_stats) return;
            std::lock_guard<std::mutex> lock(*mtx);
            agg_stats->merge(s);
        };

        client->stream_query(query, shard_timeout_ms, on_result, on_stats);
        alive_workers->fetch_sub(1);
    }

public:
    // config file format:
    //   timeout_ms <int>
    //   shard_timeout_ms <int>
    //   results_needed <int>
    //   good_threshold <double>   (optional, default 0.3)
    //   <host>:<port>
    //   ...
    // blank lines and # comments are ignored.
    QueryDistributor(const std::string& config_path)
        : global_timeout_ms(5000),
          shard_timeout_ms(4000),
          results_needed(20),
          good_threshold(0.3) {

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

            if (token == "timeout_ms") {
                iss >> global_timeout_ms;
            } else if (token == "shard_timeout_ms") {
                iss >> shard_timeout_ms;
            } else if (token == "results_needed") {
                iss >> results_needed;
            } else if (token == "good_threshold") {
                iss >> good_threshold;
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
                  << "shard timeout " << shard_timeout_ms << "ms, "
                  << "need " << results_needed << " good results (score >= "
                  << good_threshold << ")" << std::endl;
    }

    bool has_shards() const { return !shards.empty(); }

    std::vector<SearchResult> search(const std::string& query_str,
                                      SearchStats* stats = nullptr) {
        std::vector<SearchResult> all_results;
        std::mutex mtx;
        std::atomic<int> good_count(0);
        std::atomic<bool> stop_flag(false);
        std::atomic<int> alive_workers(static_cast<int>(shards.size()));

        // launch one worker per shard
        std::vector<std::thread> threads;
        threads.reserve(shards.size());
        for (size_t i = 0; i < shards.size(); i++) {
            threads.emplace_back(worker,
                &shards[i], static_cast<int>(i), std::cref(query_str),
                shard_timeout_ms, good_threshold,
                &all_results, &mtx, &good_count, &stop_flag,
                stats, &alive_workers);
        }

        // watcher: signal stop when we have enough good results OR
        // when every shard has finished OR when the global deadline passes
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(global_timeout_ms);

        while (std::chrono::steady_clock::now() < deadline) {
            if (good_count.load() >= results_needed) {
                stop_flag.store(true);
                break;
            }
            if (alive_workers.load() == 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // deadline reached (or early stop signaled). tell workers to stop.
        stop_flag.store(true);

        for (auto& t : threads) t.join();

        std::cerr << "[distributor] collected " << all_results.size()
                  << " results (" << good_count.load() << " good) from "
                  << shards.size() << " shards" << std::endl;

        // merge-sort by combined score, descending
        std::sort(all_results.begin(), all_results.end(),
            [](const SearchResult& a, const SearchResult& b) {
                return a.combined_score > b.combined_score;
            });

        // dedup by url — keep highest-scoring version
        std::unordered_set<std::string> seen;
        std::vector<SearchResult> deduped;
        deduped.reserve(all_results.size());
        for (auto& r : all_results) {
            if (seen.insert(r.url).second) {
                deduped.push_back(std::move(r));
            }
        }

        if (deduped.size() > static_cast<size_t>(results_needed)) {
            deduped.resize(results_needed);
        }
        return deduped;
    }

    // phase-2: fetch snippets for a page of results. groups by origin shard,
    // fires one /snippet request per shard, stitches results back by doc_id.
    // modifies the results in place
    void fetch_page_snippets(const std::string& raw_query,
                              std::vector<SearchResult>& page) {
        // group doc_ids by shard
        std::unordered_map<int, std::vector<int>> shard_docs;
        for (const auto& r : page) {
            if (r.shard_idx >= 0) shard_docs[r.shard_idx].push_back(r.doc_id);
        }

        // fetch per-shard (sequential for simplicity; could thread later)
        std::unordered_map<int, std::unordered_map<int, std::string>> shard_snippets;
        for (auto& [idx, doc_ids] : shard_docs) {
            if (idx < 0 || idx >= static_cast<int>(shards.size())) continue;
            shard_snippets[idx] = shards[idx].fetch_snippets(
                raw_query, doc_ids, shard_timeout_ms);
        }

        // stitch back
        for (auto& r : page) {
            if (r.shard_idx < 0) continue;
            auto sit = shard_snippets.find(r.shard_idx);
            if (sit == shard_snippets.end()) continue;
            auto dit = sit->second.find(r.doc_id);
            if (dit != sit->second.end()) r.snippet = std::move(dit->second);
        }
    }
};
