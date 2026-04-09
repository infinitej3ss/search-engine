#pragma once

#include "initializer.h"
#include <fstream>
#include <iostream>


// usage: ./crawler <config file> <seedlist> <page data dir> <frontier dir> <bloom filter dir>
int main(int argc, char** argv){
    
    // add config file endpoints to vector
    if (initialize_peers(argv[1]) != 0) return 1;

    // initialize frontier
    if (ingest_seedlist(argv[2]) != 0) return 1;

    // initialize directories
    initialize_page_file_dir(std::string(argv[3]));
    initialize_frontier_file_dir(std::string(argv[4]));
    initialize_bloom_filter_dir(std::string(argv[5]));
    load_frontier_filters();
    
    // initialize connections to peers
    establish_peer_connections();

    // manage worker threads

    // manage connections with peers
}

int initialize_peers(const char* config_file){
    std::ifstream file(config_file);

    // file opening error checking
    if (!file.is_open()) {
        std::cerr << "Error: Could not open config file." << std::endl;
        return 1;
    }

    // adding lines to endpoints vector
    std::string currentLine;
    while (std::getline(file, currentLine)) {

        // remove carriage return character
        if (!currentLine.empty() && currentLine.back() == '\r') {
            currentLine.pop_back();
        }

        // skip empty lines
        if (currentLine.empty()) {
            continue; 
        }

        // process endpoint data
        size_t colon_pos = currentLine.find(':');
        std::string ip = currentLine.substr(0, colon_pos);
        int port = std::stoi(currentLine.substr(colon_pos + 1));

        Peer newPeer = {ip, port, -1, DISCONNECTED, {}};
        peers.push_back(newPeer);
    }

    return 0;
}

int8_t ingest_seedlist(const char* seedlist){
    std::ifstream file(seedlist);

    // file opening error checking
    if (!file.is_open()) {
        std::cerr << "Error: Could not open seedlist file." << std::endl;
        return 1;
    }

    // adding lines to endpoints vector
    std::string currentLine;
    while (std::getline(file, currentLine)) {

        // remove carriage return character
        if (!currentLine.empty() && currentLine.back() == '\r') {
            currentLine.pop_back();
        }

        // skip empty lines
        if (currentLine.empty()) {
            continue; 
        }
        
        FrontierUrl newUrl = {0, currentLine};
        insert_url(newUrl);
    }

    return 0;
}