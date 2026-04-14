#pragma once

#include <netinet/in.h>

#include <string>
#include <vector>

#include "BloomFilter.h"
#include "frontier.h"
#include "page_data.h"
#include "robots.txt/RobotsCache.h"

struct Peer {
    std::string ip_address;   // To know where to reconnect
    int port;                 // To know where to reconnect
    std::vector<FrontierUrl> url_send_buffer; // Data waiting to be sent
};

inline RobotsCache robotsCache;

// add endpoints from config file to endpoints vector
int initialize_peers(std::string& config_file);

// block until all peers have confirmed their connection
void establish_peer_connections();

// read seedlist into frontier
int8_t ingest_seedlist(const char* seedlist);