#pragma once

// leader-side client for talking to a single index shard.
// sends a query over http, reads back newline-delimited json results.
//
// url parsing and socket code adapted from GLurl (LinuxGetUrl.cpp)
// and the crawler's get_ssl.cpp.

#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "search_result.hpp"

// url parser from GLurl assignment (LinuxGetUrl.cpp)
// extracts service, host, port, and path from a url string
class ParsedUrlHttp {
public:
    const char *CompleteUrl;
    char *Service, *Host, *Port, *Path;

    ParsedUrlHttp( const char *url ) {
        CompleteUrl = url;

        pathBuffer = new char[ strlen( url ) + 1 ];
        const char *f;
        char *t;
        for ( t = pathBuffer, f = url; *t++ = *f++; )
            ;

        Service = pathBuffer;

        const char Colon = ':', Slash = '/';
        char *p;
        for ( p = pathBuffer; *p && *p != Colon; p++ )
            ;

        if ( *p )
            {
            *p++ = 0;

            if ( *p == Slash )
                p++;
            if ( *p == Slash )
                p++;

            Host = p;

            for ( ; *p && *p != Slash && *p != Colon; p++ )
                ;

            if ( *p == Colon )
                {
                *p++ = 0;
                Port = +p;
                for ( ; *p && *p != Slash; p++ )
                    ;
                }
            else
                Port = p;

            if ( *p )
                *p++ = 0;

            Path = p;
            }
        else
            Host = Path = p;
        }

    ~ParsedUrlHttp( ) {
        delete[ ] pathBuffer;
    }

private:
    char *pathBuffer;
};

struct ShardConfig {
    std::string host;
    int port;
};

class ShardClient {
private:
    ShardConfig config;

    // socket connection adapted from LinuxGetUrl.cpp
    // connects to host:port, returns socket fd or -1
    int connect_to_shard() {
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
            std::cerr << "[shard_client] failed to create socket" << std::endl;
            freeaddrinfo(serverInfo);
            return -1;
        }

        // timeouts from crawler's get_ssl.cpp
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        setsockopt(socketFD, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        setsockopt(socketFD, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);

        int connectResult = connect(socketFD, serverInfo->ai_addr,
                                    serverInfo->ai_addrlen);
        if (connectResult == -1) {
            std::cerr << "[shard_client] connect failed: "
                      << strerror(errno) << std::endl;
            close(socketFD);
            freeaddrinfo(serverInfo);
            return -1;
        }

        freeaddrinfo(serverInfo);
        return socketFD;
    }

    // http request/response pattern from LinuxGetUrl.cpp
    // sends GET, reads response, returns body
    std::string http_get(int socketFD, const std::string& path) {
        // build request (same structure as LinuxGetUrl.cpp)
        std::string request;
        request += "GET /";
        request += path;
        request += " HTTP/1.1\r\n";
        request += "Host: ";
        request += config.host;
        request += "\r\n";
        request += "Connection: close\r\n";
        request += "\r\n";

        int bytesSent = send(socketFD, request.c_str(), request.length(), 0);
        if (bytesSent == -1) {
            std::cerr << "[shard_client] send failed: "
                      << strerror(errno) << std::endl;
            return "";
        }

        // read response, find header/body boundary
        // (adapted from LinuxGetUrl.cpp's recv loop)
        char buffer[8192];
        std::string response;
        ssize_t bytesRead;

        while ((bytesRead = recv(socketFD, buffer, sizeof(buffer), 0)) > 0) {
            response.append(buffer, bytesRead);
        }

        // extract body (everything after \r\n\r\n)
        size_t headerEnd = response.find("\r\n\r\n");
        if (headerEnd == std::string::npos) return "";
        return response.substr(headerEnd + 4);
    }

    // minimal json line parser for newline-delimited results
    bool parse_result_line(const std::string& line, SearchResult& result) {
        if (line.empty() || line[0] != '{') return false;

        auto extract_int = [&](const std::string& key) -> int {
            std::string needle = "\"" + key + "\":";
            size_t pos = line.find(needle);
            if (pos == std::string::npos) return 0;
            return std::stoi(line.substr(pos + needle.size()));
        };

        auto extract_double = [&](const std::string& key) -> double {
            std::string needle = "\"" + key + "\":";
            size_t pos = line.find(needle);
            if (pos == std::string::npos) return 0.0;
            return std::stod(line.substr(pos + needle.size()));
        };

        auto extract_string = [&](const std::string& key) -> std::string {
            std::string needle = "\"" + key + "\":\"";
            size_t pos = line.find(needle);
            if (pos == std::string::npos) return "";
            size_t start = pos + needle.size();
            size_t end = line.find('"', start);
            if (end == std::string::npos) return "";
            return line.substr(start, end - start);
        };

        result.doc_id = extract_int("doc_id");
        result.url = extract_string("url");
        result.static_score = extract_double("static_score");
        result.dynamic_score = extract_double("dynamic_score");
        result.combined_score = extract_double("combined_score");

        return true;
    }

public:
    ShardClient(const ShardConfig& cfg) : config(cfg) {}

    // query this shard. sends GET /query?q=...&timeout_ms=...,
    // parses newline-delimited json response.
    std::vector<SearchResult> query(const std::string& query_str, int timeout_ms) {
        std::vector<SearchResult> results;

        int fd = connect_to_shard();
        if (fd < 0) return results;

        // url-encode spaces as +
        std::string encoded_query;
        for (char c : query_str) {
            if (c == ' ') encoded_query += '+';
            else encoded_query += c;
        }

        std::string path = "query?q=" + encoded_query
                         + "&timeout_ms=" + std::to_string(timeout_ms);

        std::string body = http_get(fd, path);
        close(fd);

        // parse newline-delimited json
        std::istringstream stream(body);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            SearchResult r;
            if (parse_result_line(line, r)) {
                results.push_back(r);
            }
        }

        return results;
    }
};
