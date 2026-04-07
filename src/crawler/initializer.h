#pragma once

#include "frontier.h"

#include <string>
#include <vector>
#include <netinet/in.h>

std::vector<std::string> endpoints;

enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED
};

struct Peer {
    std::string node_id;      // A logical name for the machine (e.g., "Worker-1")
    std::string ip_address;   // To know where to reconnect
    int port;                 // To know where to reconnect
    
    int socket_fd;            // The active file descriptor (e.g., -1 if disconnected)
    ConnectionState state;    // Current status of the connection
    
    std::vector<FrontierUrl> url_send_buffer; // Data waiting to be sent
};
