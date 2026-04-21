#pragma once

// url utility functions adapted from crawler's get_ssl.cpp

#include <cctype>
#include <cstdio>
#include <string>

// extract the authority (domain) from a url, stripping scheme, auth, port, path.
// from crawler branch: src/crawler/get_ssl.cpp
inline std::string extract_authority(const std::string& url) {
    size_t start_pos = 0;

    // skip the scheme
    size_t scheme_pos = url.find("://");
    if (scheme_pos != std::string::npos) {
        start_pos = scheme_pos + 3;
    }

    // find the end of the host/domain block
    size_t end_pos = url.find_first_of("/?#", start_pos);

    // extract everything between the scheme and the path
    std::string authority = url.substr(start_pos, end_pos - start_pos);

    // remove user authentication if present
    size_t auth_pos = authority.find('@');
    if (auth_pos != std::string::npos) {
        authority = authority.substr(auth_pos + 1);
    }

    // remove the port number if present
    size_t port_pos = authority.find(':');
    if (port_pos != std::string::npos) {
        authority = authority.substr(0, port_pos);
    }

    return authority;
}

// percent-encode a query string value. encodes everything except unreserved
inline std::string url_encode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
            out += static_cast<char>(c);
        } else if (c == ' ') {
            out += '+';
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// decode percent-encoded and '+'-as-space values
inline std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size()) {
            int hi = 0, lo = 0;
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            hi = hex(s[i + 1]);
            lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
            } else {
                out += s[i];
            }
        } else {
            out += s[i];
        }
    }
    return out;
}
