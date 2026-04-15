#!/usr/bin/env bash

# make directories
mkdir crawler_test_files/page_data_dir crawler_test_files/frontier_dir crawler_test_files/bloom_filter_dir

# Navigate to crawler directory
cd search-engine/src/crawler

# 3. Call cmake
cmake --build build

# 4. Navigate to build directory
cd ../../build

# 5. Launch crawler
./crawler src/init.config crawler_test_files/seedlist.txt crawler_test_files/page_data_dir/ crawler_test_files/frontier_dir/ crawler_test_files/bloom_filter_dir/ 200 $1
