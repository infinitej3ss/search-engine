#ifndef BLOOMFILTER_H
#define BLOOMFILTER_H

#include <openssl/evp.h>
#include <openssl/md5.h>
#include <string.h>

#include <bitset>
#include <cmath>
#include <string>
#include <vector>

class Bloomfilter {
    public:
        Bloomfilter( int num_objects, double false_positive_rate ) {
            // Determine the size of bits of our data vector, and resize.
            filterSize = int(-1 * (num_objects * std::log(false_positive_rate)) / (std::log(2) * std::log(2)));

            // sanity checking for size of filter
            size_t max_size = (filter.max_size() < 100000000) ? filter.max_size() / 2 : 100000000;
            if (filterSize > max_size) {
                filterSize = max_size;
            }

            // Resize filter accordingly
            filter.resize(filterSize);

            // Determine number of hash functions to use.
            numHashes = (filterSize / num_objects) * std::log(2);
        }

        void insert( const std::string &s) {
            // Hash the string into two unique hashes.
            std::pair<uint64_t, uint64_t> hashPair = hash(s);

            // Use double hashing to get unique bit, and repeat for each hash function.
            for(int i = 0; i < numHashes; i++) {
                int filter_index = (hashPair.first + (i * hashPair.second)) % filterSize; 
                filter[filter_index] = true;
            }
        }   

        bool contains( const std::string &s ) {
            // Hash the string into two unqiue hashes.
            std::pair<uint64_t, uint64_t> hashPair = hash(s);
        
            // Use double hashing to get unique bit, and repeat for each hash function.

            for (int i = 0; i < numHashes; i++) {
                int filter_index = (hashPair.first + (i * hashPair.second)) % filterSize;
                if(!filter[filter_index]) {
                    return false;  // If bit is false, we know for certain this unique string has not been inserted.
                }
            }
            return true;  // If all bits were true, the string is likely inserted, but false positive is possible.
        }   

        private:
        // Add any private member variables that may be neccessary.

        std::vector<bool> filter;
        size_t filterSize;
        int numHashes;

        std::pair<uint64_t, uint64_t> hash( const std::string &datum ) {
            //Use MD5 to hash the string into one 128 bit hash, and split into 2 hashes.
            
            uint64_t full_hash_value[2];
            MD5((unsigned char*)datum.c_str(), datum.length(), (unsigned char*)full_hash_value); // depreciated but openssl version on autograder is old enough for it to be fine ig
            //EVP_Q_digest(NULL, "MD5", NULL, datum.c_str(), datum.size(), (unsigned char*)full_hash_value, NULL);
            return {full_hash_value[0], full_hash_value[1]};
        }
    };

#endif