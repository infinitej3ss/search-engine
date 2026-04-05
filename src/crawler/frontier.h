#pragma once

#include <string>
#include <vector>

struct FrontierUrl {
    u_int32_t distance_from_seedlist;
    std::string url;
};

// inserts url into the frontier
void insert_url(FrontierUrl& url);

// insert vector of urls
void insert_url_vector(std::vector<FrontierUrl> url_vector);

// adds a url's domain to blacklist, preventing it from being inserted or retreived from the frontier
void blacklist_domain(std::string &url);

// retreives url from the frontier
FrontierUrl get_url();

// writes a vector of FrontierUrls to buffer and increments buffer past the end of the vector
void serialize_frontier_url_vector(void** buffer, const std::vector<FrontierUrl>& v);

// reads a vector of FrontierUrls from buffer and increments buffer past the end of the vector
std::vector<FrontierUrl> deserialize_frontier_url_vector(void** buffer);