#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <csignal>

#include "BloomFilter.h"
#include "frontier.h"
#include "initializer.h"
#include "page_data.h"
#include "worker_thread.h"
#include "link_distributor.h"

// usage: ./crawler <config file> <seedlist> <page data dir> <frontier dir> <bloom filter dir> <worker thread count> <machine ID> <should load seedlist>
int main(int argc, char** argv) {

    signal(SIGPIPE, SIG_IGN); // don't crash on broken pipes

    // add config file endpoints to vector
    std::string configFile = argv[1];
    if (initialize_peers(configFile) != 0) return 1;

    int should_ingest = atoi(argv[8]);
    machineID = atoi(argv[7]);  // read in machineID
    // initialize frontier
    if (should_ingest) {
        if (ingest_seedlist(argv[2]) != 0) return 1;
    }
    // initialize directories
    initialize_page_file_dir(std::string(argv[3]));
    initialize_frontier_file_dir(std::string(argv[4]));
    initialize_bloom_filter_dir(std::string(argv[5]));
    load_frontier_filters();

    // distribute links
    pthread_t server_thread;
    if (pthread_create(&server_thread, nullptr, start_distribution_server, nullptr) != 0){
        std::cerr << "Error creating distribution server" << std::endl;
        return 1;
    }

    // manage worker threads
    int thread_count = atoi(argv[6]);
    std::vector<pthread_t> threads(thread_count);
    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&threads[i], nullptr, run_worker_thread, nullptr) != 0) {
            std::cerr << "Error creating thread " << i << std::endl;
            return 1;
        }
    }

    // await user input
    bool continue_running = true;
    u_int64_t num;
    while (continue_running) {
        char input;
        std::cin >> input;
        switch (input) {
            case 'n':
                num = get_num_crawled_pages();
                std::cout << "crawled " << std::to_string(num) << " pages\n";
                break;

            case 's':
                stop_crawling();
                num = get_num_crawled_pages();
                std::cout << "stopped crawling\n crawled " << std::to_string(num) << " pages\n";
                for(auto &t : threads) {
                    pthread_join(t, nullptr);
                }
                std::cout << "all thread stopped\n";
                for(int i = 0; i < NUM_PAGE_FILE_RANKS; i++) {
                    write_page_file(i);
                }
                write_frontier_filters();
                std::cout << "all page files written\n";
                break;

            case 'e':
                stop_crawling();
                continue_running = false;
                break;

            case 'h':
                std::cout << "available commands:\n h - print a help message\n n - print number pages crawled\n s - stop crawling\n e - exit program\n";
                break;

            default:
                std::cout << "unrecognized command, enter h for available commands\n";
                break;
        }
    }

    // close connections
}