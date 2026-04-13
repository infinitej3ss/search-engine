#pragma once

#include "frontier.h"
#include "page_data.h"
#include "BloomFilter.h"

#include <string>
#include <vector>
#include <netinet/in.h>

enum ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED
};

struct Peer {
    std::string ip_address;   // To know where to reconnect
    int port;                 // To know where to reconnect
    
    int socket_fd;            // The active file descriptor (e.g., -1 if disconnected)
    ConnectionState state;    // Current status of the connection
    
    std::vector<FrontierUrl> url_send_buffer; // Data waiting to be sent
};

std::vector<Peer> peers;

// add endpoints from config file to endpoints vector
int initialize_peers(std::string& config_file);

// block until all peers have confirmed their connection
void establish_peer_connections();

// read seedlist into frontier
int8_t ingest_seedlist(const char* seedlist);