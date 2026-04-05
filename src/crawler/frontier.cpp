#include "frontier.h"

#include <cstring>
#include <utility>

#include "page_data.h"

const u_int64_t NUM_FRONTIER_QUEUES = 10;
pthread_mutex_t FRONTIER_INSERTION_MUTEX = PTHREAD_MUTEX_INITIALIZER;

// internal url insertion without locking mutex
void insert_url_nonatomic(FrontierUrl& url) {
}

// inserts url into the frontier
void insert_url(FrontierUrl& url) {
    pthread_mutex_lock(&FRONTIER_INSERTION_MUTEX);
    insert_url_nonatomic(url);
    pthread_mutex_unlock(&FRONTIER_INSERTION_MUTEX);
}

// insert vector of urls
void insert_url_vector(std::vector<FrontierUrl> url_vector) {
    pthread_mutex_lock(&FRONTIER_INSERTION_MUTEX);
    for (auto& url : url_vector) {
        insert_url_nonatomic(url);
    }
    pthread_mutex_unlock(&FRONTIER_INSERTION_MUTEX);
}

// adds url to blacklist, preventing it from being inserted or retreived from the frontier
void blacklist_url(std::string& url) {
    // TODO: implement
    return;
}

// retreives url from the frontier
FrontierUrl get_url() {
    // TODO: implement
    FrontierUrl frontier_url;
    frontier_url.distance_from_seedlist = 0;
    frontier_url.url = "https://www.google.com";
    return frontier_url;
}

// writes a vector of frontier urls to buffer and increments buffer past the end of the vector
void serialize_frontier_url_vector(void** buffer, const std::vector<FrontierUrl>& v) {
    u_int64_t size = v.size();

    // write size of vector
    memcpy(*buffer, &size, sizeof(u_int64_t));
    *buffer += sizeof(u_int64_t);

    // write frontier urls
    for (auto& s : v) {
        memcpy(*buffer, &s.distance_from_seedlist, sizeof(u_int32_t));
        *buffer += sizeof(u_int32_t);
        serialize_string(buffer, s.url);
    }
}

// reads a vector of frontier urls from buffer and increments buffer past the end of the vector
std::vector<FrontierUrl> deserialize_frontier_url_vector(void** buffer) {
    std::vector<FrontierUrl> v;
    u_int64_t size;

    // resize vector to serialized value
    memcpy(&size, *buffer, sizeof(u_int64_t));
    *buffer += sizeof(u_int64_t);
    v.resize(size);

    // read strings
    for (u_int64_t i = 0; i < size; i++) {
        memcpy(&v.at(i).distance_from_seedlist, *buffer, sizeof(u_int32_t));
        *buffer += sizeof(u_int32_t);
        v.at(i).url = deserialize_string(buffer);
    }

    return std::move(v);
}