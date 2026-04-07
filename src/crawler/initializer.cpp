#pragma once

#include "initializer.h"

// usage: ./crawler <host endpoint 1> <host endpoint 2> ... <host endpoint n>
int main(int argc, char** argv){
    
    // add endpoints to vector
    for (int i = 1; i < argc; i++){
        endpoints.push_back(argv[i]);
    }

    
}