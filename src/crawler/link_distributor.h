#pragma once
#include "string"
#include "frontier.h"

enum URL_destination {
    frontier,
    remote_host
};

const int URL_BATCH_SIZE = 10000;

inline int machineID;

inline std::vector<Peer> peers;

// gets whether a URL is for the frontier or a remote machine, writes to destinationID
URL_destination get_URL_destination(std::string &url, int &destinationID);

// manages URLs that are sent to remote peers
void send_URL_to_remote_peer(FrontierUrl &url, int machineID);

// send a specified machine its list of links and clear it
void send_remote_peer_URL_vector(int machineID);

// listens on a loop for peers trying to send URL vectors
void* start_distribution_server(void* arg);