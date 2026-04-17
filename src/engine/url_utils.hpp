#pragma once

// url utility functions adapted from crawler's get_ssl.cpp

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
