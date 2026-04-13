#include "link_distributor.h"

#include <functional>  // Required for std::hash
#include <vector>

#include "get_ssl.h"
#include "initializer.h"

// maps a URL to a machine ID
int URL_to_destination(const std::string &url) {
    int numHosts = peers.size();
    if (numHosts == 0) return -1;

    // Use C++ built-in string hashing
    std::hash<std::string> hasher;
    size_t hash_val = hasher(extract_authority(url));

    return hash_val % numHosts;
}

// gets whether a URL is for the frontier or a remote machine
URL_destination get_URL_destination(std::string &url){
    // TODO: implement

    return frontier;
}

// manages URLs that are sent to remote peers
void send_URL_to_remote_peer(FrontierUrl &url, int machineID){
    // TODO: implement

    // append url to file for the target machine

    // if the file is large enough, send it via send_peer_URL_vector()
}

// send a specified machine its list of links and clear it
void send_remote_peer_URL_vector(int machineID){
    
    // serialize file data into buffer

    // open up TCP connection to machine

    // send file to machine

    // wait for ack from machine

    // clear file
}

void start_distribution_server(){
    // TODO: implement
}