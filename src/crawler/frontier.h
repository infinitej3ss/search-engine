#pragma once

#include <string>
#include <vector>

struct FrontierUrl {
    u_int32_t distance_from_seedlist;
    std::string url;
};

// inserts url into the frontier
//
// Returns:
//      0 on success, -1 on failure
int insert_url(FrontierUrl& url);

// insert vector of urls
//
// Returns:
//      0 on success, -1 on failure
int insert_url_vector(std::vector<FrontierUrl>& url_vector);

// adds a url's domain to blacklist, preventing it from being inserted or retreived from the frontier
void blacklist_domain(std::string &url);

// retreives url from the frontier
FrontierUrl get_url();

// writes a vector of FrontierUrls to buffer and increments buffer past the end of the vector
void serialize_frontier_url_vector(void** buffer, const std::vector<FrontierUrl>& v);

// reads a vector of FrontierUrls from buffer and increments buffer past the end of the vector
std::vector<FrontierUrl> deserialize_frontier_url_vector(void** buffer);

// initializes the frontier file directory
//
// Parameters:
//      dir - directory to initialize from
//
// Returns:
//      0 on success, -1 on failure
int initialize_frontier_file_dir(const std::string& dir);

// calculates size of a vector of frontier urls after serialization
u_int64_t serialized_frontier_url_vector_size(const std::vector<FrontierUrl> &v);

int write_frontier_filters();

int load_frontier_filters();