#include "initializer.h"
#include "robots.txt/RobotsCache.h"
#include "worker_thread.h"
#include <fstream>
#include <iostream>

// TODO: Declare RobotsCache object
RobotsCache robotsCache;

// usage: ./crawler <config file> <seedlist> <page data dir> <frontier dir> <bloom filter dir> <worker thread count>
int main(int argc, char** argv){
    
    // add config file endpoints to vector
    std::string configFile = argv[1];
    if (initialize_peers(configFile) != 0) return 1;

    // initialize frontier
    if (ingest_seedlist(argv[2]) != 0) return 1;

    // initialize directories
    initialize_page_file_dir(std::string(argv[3]));
    initialize_frontier_file_dir(std::string(argv[4]));
    initialize_bloom_filter_dir(std::string(argv[5]));
    load_frontier_filters();
    
    // connect to peers
    establish_peer_connections();

    // manage worker threads
    int thread_count = atoi(argv[6]);
    vector<pthread_t> threads(thread_count);
    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&threads[i], nullptr, run_worker_thread, nullptr) != 0) {
            cerr << "Error creating thread " << i << endl;
            return 1;
        }
    }

    for(auto &t : threads) {
        pthread_detach(t);
    }

    // manage connections with peers

    // await user input
    bool continue_running = true;
    while (continue_running) {
        char input;
        cin >> input;
        switch(input) {
            case 'n': 
            u_int64_t num = get_num_crawled_pages();
            cout << "crawled " << std::to_string(num) << " pages\n";
            break;

            case 's':
            stop_crawling();
            u_int64_t num = get_num_crawled_pages();
            cout << "stopped crawling\n crawled " << std::to_string(num) << " pages\n";
            break;

            case 'e':
            stop_crawling();
            continue_running = false;
            break;

            case 'h':
            cout << "available commands:\n h - print a help message\n n - print number pages crawled\n s - stop crawling\n e - exit program\n";
            break;

            default:
            cout << "unrecognized command, enter h for available commands\n";
            break;
        }
    }

    // close connections
}

int initialize_peers(const std::string &config_file){
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