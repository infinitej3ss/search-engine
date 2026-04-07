#include "string"

enum URL_destination {
    frontier,
    remote_host
};


// given a URL, either pass it to the frontier or distribute to another machine
void process_new_url(std::string &url, u_int64_t distance_from_seedlist);

// send a file 
void send_peer_URLs(int machineID);

// maps a URL to a machine ID
int URL_to_destination(std::string &url);

// gets whether a URL is for the frontier or a remote machine
URL_destination get_URL_destination(std::string &url);

// manages URLs that are sent to the frontier
void send_URL_to_frontier(std::string &url, u_int64_t distance_from_seedlist);

// manages URLs that are sent to remote hosts
void send_URL_to_remote_host(std::string &url, u_int64_t distance_from_seedlist, int machineID);