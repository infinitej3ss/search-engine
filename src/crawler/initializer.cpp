#pragma once

#include "initializer.h"
#include <fstream>
#include <iostream>


// usage: ./crawler <config file> <seedlist>
int main(int argc, char** argv){
    
    // add endpoints to vector
    int returnCode = add_endpoints(argv[1]);
    if (returnCode != 0) return returnCode;

    // initialize frontier
    returnCode = ingest_seedlist(argv[2]);
    if (returnCode != 0) return returnCode;

    // initialize connections to peers

    // manage worker threads

    // manage connections with peers
}

int add_endpoints(const char* config_file){
    std::ifstream file(config_file);

    // file opening error checking
    if (!file.is_open()) {
        std::cerr << "Error: Could not open config file." << std::endl;
        return 1;
    }

    // adding lines to endpoints vector
    std::string currentLine;
    while (std::getline(file, currentLine)) {
        endpoints.push_back(currentLine);
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
        insert_seed_list(newUrl); // skips saving to file
    }

    return 0;
}