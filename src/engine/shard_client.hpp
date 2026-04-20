#pragma once

// leader-side client for talking to a single index shard.
// streams newline-delimited json results as they arrive. caller provides
// a callback invoked per result; returning false from the callback signals
// "stop — leader's had enough" and the client closes the socket early.
//
// url parsing and socket code adapted from GLurl (LinuxGetUrl.cpp)
// and the crawler's get_ssl.cpp.

#include <string>
#include <functional>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "search_result.hpp"

struct ShardConfig {
    std::string host;
    int port;
};

class ShardClient {
private:
    ShardConfig config;

    // connect to host:port, return socket fd or -1.
    // applies SO_RCVTIMEO so a stuck shard doesn't block us forever.
    int connect_to_shard(int recv_timeout_ms) {
        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo* serverInfo = nullptr;
        std::string port_str = std::to_string(config.port);

        int status = getaddrinfo(config.host.c_str(), port_str.c_str(),
                                 &hints, &serverInfo);
        if (status != 0) {
            std::cerr << "[shard_client] dns error: "
                      << gai_strerror(status) << std::endl;
            return -1;
        }

        int socketFD = socket(serverInfo->ai_family,
                              serverInfo->ai_socktype,
                              serverInfo->ai_protocol);
        if (socketFD == -1) {
            std::cerr << "[shard_client] socket() failed" << std::endl;
            freeaddrinfo(serverInfo);
            return -1;
        }

        // recv timeout — if shard goes silent, we don't wait forever
        struct timeval timeout;
        timeout.tv_sec = recv_timeout_ms / 1000;
        timeout.tv_usec = (recv_timeout_ms % 1000) * 1000;
        setsockopt(socketFD, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        setsockopt(socketFD, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);

        if (connect(socketFD, serverInfo->ai_addr,
                    serverInfo->ai_addrlen) == -1) {
            std::cerr << "[shard_client] connect failed: "
                      << strerror(errno) << std::endl;
            close(socketFD);
            freeaddrinfo(serverInfo);
            return -1;
        }

        freeaddrinfo(serverInfo);
        return socketFD;
    }

    bool send_request(int fd, const std::string& path) {
        std::string req = "GET /" + path + " HTTP/1.1\r\n"
                        + "Host: " + config.host + "\r\n"
                        + "Connection: close\r\n\r\n";
        ssize_t sent = send(fd, req.c_str(), req.size(), 0);
        return sent == static_cast<ssize_t>(req.size());
    }

    // minimal json extractors. scan the line for "key":value shape
    static int extract_int(const std::string& line, const std::string& key) {
        std::string needle = "\"" + key + "\":";
        size_t pos = line.find(needle);
        if (pos == std::string::npos) return 0;
        return std::stoi(line.substr(pos + needle.size()));
    }

    static double extract_double(const std::string& line, const std::string& key) {
        std::string needle = "\"" + key + "\":";
        size_t pos = line.find(needle);
        if (pos == std::string::npos) return 0.0;
        return std::stod(line.substr(pos + needle.size()));
    }

    static std::string extract_string(const std::string& line, const std::string& key) {
        std::string needle = "\"" + key + "\":\"";
        size_t pos = line.find(needle);
        if (pos == std::string::npos) return "";
        size_t start = pos + needle.size();
        size_t end = line.find('"', start);
        if (end == std::string::npos) return "";
        return line.substr(start, end - start);
    }

    // parse a bracketed int array: "key":[1,2,3]
    static std::vector<int> extract_int_array(const std::string& line,
                                               const std::string& key) {
        std::vector<int> out;
        std::string needle = "\"" + key + "\":[";
        size_t pos = line.find(needle);
        if (pos == std::string::npos) return out;
        size_t start = pos + needle.size();
        size_t end = line.find(']', start);
        if (end == std::string::npos) return out;
        std::string body = line.substr(start, end - start);
        size_t i = 0;
        while (i < body.size()) {
            size_t comma = body.find(',', i);
            std::string tok = body.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
            if (!tok.empty()) {
                try { out.push_back(std::stoi(tok)); } catch (...) {}
            }
            if (comma == std::string::npos) break;
            i = comma + 1;
        }
        return out;
    }

    bool parse_result_line(const std::string& line, SearchResult& result) {
        if (line.empty() || line[0] != '{') return false;

        result.doc_id         = extract_int(line, "doc_id");
        result.url            = extract_string(line, "url");
        result.title          = extract_string(line, "title");
        result.snippet        = extract_string(line, "snippet");
        result.static_score   = extract_double(line, "static_score");
        result.dynamic_score  = extract_double(line, "dynamic_score");
        result.combined_score = extract_double(line, "combined_score");
        result.t1             = extract_double(line, "t1");
        result.t2             = extract_double(line, "t2");
        result.t3             = extract_double(line, "t3");
        result.bm25           = extract_double(line, "bm25");
        return true;
    }

    static bool parse_stats_line(const std::string& line, SearchStats& stats) {
        stats.constraint_solved   = extract_int(line, "constraint_solved");
        stats.passed_static_floor = extract_int(line, "passed_static_floor");
        stats.per_rank_matched    = extract_int_array(line, "per_rank_matched");
        return true;
    }

public:
    ShardClient(const ShardConfig& cfg) : config(cfg) {}

    // callback is invoked for each result as it arrives.
    // returning false from the callback means "stop — close the socket."
    // returns the number of results delivered
    using ResultCallback = std::function<bool(const SearchResult&)>;

    // optional second callback: invoked at most once per query, when the
    // shard emits its trailing "_type":"stats" ndjson line
    using StatsCallback = std::function<void(const SearchStats&)>;

    int stream_query(const std::string& query_str,
                     int recv_timeout_ms,
                     const ResultCallback& on_result,
                     const StatsCallback& on_stats = nullptr) {
        int fd = connect_to_shard(recv_timeout_ms);
        if (fd < 0) return 0;

        // url-encode spaces as +
        std::string encoded;
        for (char c : query_str) {
            encoded += (c == ' ' ? '+' : c);
        }

        std::string path = "query?q=" + encoded;

        if (!send_request(fd, path)) {
            close(fd);
            return 0;
        }

        // read response, stripping http headers on first chunk
        std::string buffer;
        char recv_buf[4096];
        bool headers_done = false;
        int delivered = 0;

        while (true) {
            ssize_t n = recv(fd, recv_buf, sizeof(recv_buf), 0);
            if (n <= 0) break;
            buffer.append(recv_buf, n);

            if (!headers_done) {
                size_t end = buffer.find("\r\n\r\n");
                if (end == std::string::npos) continue;
                buffer.erase(0, end + 4);
                headers_done = true;
            }

            // drain any complete lines in the buffer
            bool stop = false;
            while (true) {
                size_t nl = buffer.find('\n');
                if (nl == std::string::npos) break;

                std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);

                // stats line arrives after the last result; result lines
                // have a doc_id, stats lines carry "_type":"stats"
                if (line.find("\"_type\":\"stats\"") != std::string::npos) {
                    if (on_stats) {
                        SearchStats s;
                        parse_stats_line(line, s);
                        on_stats(s);
                    }
                    continue;
                }

                SearchResult r;
                if (parse_result_line(line, r)) {
                    delivered++;
                    if (!on_result(r)) { stop = true; break; }
                }
            }
            if (stop) break;
        }

        close(fd);
        return delivered;
    }
};
