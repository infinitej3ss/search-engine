#pragma once

#include "frontier.h"

#include <string>
#include <vector>
#include <netinet/in.h>

std::vector<Peer> peers;

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

// add endpoints from config file to endpoints vector
void initialize_peers(std::string& config_file);

// block until all peers have confirmed their connection
void establish_peer_connections();