#include "string"

#include "link_distributor.h"

// given a URL, either pass it to the frontier or distribute to another machine
void process_new_url(std::string &url, u_int64_t distance_from_seedlist) {
    // hash url and take mod (MD5)

    // check if url is for the current machine

    // if not, append url to file for the target machine

    // if the file is large enough, send it
}

// send a specified machine its list of links and clear it
void send_peer_URLs(int machineID){
    
    // serialize file data into buffer

    // open up TCP connection to machine

    // send file to machine

    // wait for ack from machine

    // clear file
}

// maps a URL to a machine ID
int URL_to_destination(std::string &url){
    // TODO: implement
}

// gets whether a URL is for the frontier or a remote machine
URL_destination get_URL_destination(std::string &url){
    // TODO: implement
}

// manages URLs that are sent to the frontier
void send_URL_to_frontier(std::string &url, u_int64_t distance_from_seedlist){
    // TODO: implement
}

// manages URLs that are sent to remote hosts
void send_URL_to_remote_host(std::string &url, u_int64_t distance_from_seedlist, int machineID){
    // TODO: implement
}