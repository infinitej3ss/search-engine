#include "frontier.h"

// inserts url into the frontier
void insert_url(FrontierUrl& url) {
    // TODO: implement
    return;
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