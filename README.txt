# Index API for Ranker Team

## Files provided:
- simple_api.h    - Include this header
- libindex.dylib  - Link against this library (macOS)
- test_ranker.cpp - Example usage

## To compile your program:
g++ --std=c++17 your_program.cpp -L. -lindex -pthread -o your_program

## Basic usage:
    IndexHandle idx = create_index();
    add_document(idx, url, titlewords, title_count, bodywords, body_count, distance);
    finalize_index(idx);
    
    int count;
    int* results = find_and_query(idx, terms, num_terms, &count);
    // use results...
    free_results(results);
    
    destroy_index(idx);

## Note: If you're on Linux, you'll need to compile from source.
## Contact us for the source files.
