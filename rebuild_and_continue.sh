#!/usr/bin/env bash

rm -rf build

cmake -S . -B build

cmake --build build

./build/crawler src/init.config crawler_test_files/seedlist.txt crawler_test_files/page_data_dir/ crawler_test_files/frontier_dir/ crawler_test_files/bloom_filter_dir/ 200 "$1" 1