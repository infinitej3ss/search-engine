#pragma once

#include <string>

struct FrontierUrl {
    u_int64_t distance_from_seedlist;
    std::string url;
};

// inserts url into the frontier
void insert_url(FrontierUrl& url);

// adds a url's domain to blacklist, preventing it from being inserted or retreived from the frontier
void blacklist_domain(std::string &url);

// retreives url from the frontier
FrontierUrl get_url();