#ifndef BLOOMFILTER_H
#define BLOOMFILTER_H

#include <vector>
#include <bitset>
#include <cmath>
#include <string>
#include <string.h>
#include <openssl/md5.h>

class Bloomfilter {
    public:
        Bloomfilter( int num_objects, double false_positive_rate ) {
            // Determine the size of bits of our data vector, and resize.
            filterSize = int(-1 * (num_objects * std::log(false_positive_rate)) / (std::log(2) * std::log(2)));
            
            // Resize filter accordingly
            filter.resize(filterSize);

            // Determine number of hash functions to use.
            numHashes = (filterSize / num_objects) * std::log(2);
        }

        void insert( const std::string &s) {
            // Hash the string into two unique hashes.
            std::pair<uint64_t, uint64_t> hashPair = hash(s);

            // Use double hashing to get unique bit, and repeat for each hash function.

        }   

        bool contains( const std::string &s ) {
            // Hash the string into two unqiue hashes.
            std::pair<uint64_t, uint64_t> hashPair = hash(s);
        
            // Use double hashing to get unique bit, and repeat for each hash function.

            // If bit is false, we know for certain this unique string has not been inserted.

            // If all bits were true, the string is likely inserted, but false positive is possible.

            // This line is for compiling, replace this with own code.
            return false;
        }   

        private:
        // Add any private member variables that may be neccessary.

        std::vector<bool> filter;
        int filterSize;
        int numHashes;

        std::pair<uint64_t, uint64_t> hash( const std::string &datum ) {
            //Use MD5 to hash the string into one 128 bit hash, and split into 2 hashes.
            
            //This line is for compiling, replace this with own code.
            return {0,0};
        }
    };

#endif