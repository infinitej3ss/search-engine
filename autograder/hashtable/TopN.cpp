// TopN.cpp
// Nicole Hamilton nham@umich.edu

// Given a hashtable, return a dynamically-allocated array
// of pointers to the top N pairs based on their values.
// Caller is responsible for deleting the array.

// Individual pairs are to be C-strings as keys and size_t's
// as values.

#include "HashTable.h"
#include "TopN.h"

using namespace std;


using Hash = HashTable< const char *, size_t >;
using Pair = Tuple< const char *, size_t >;


Pair **TopN( Hash *hashtable, int N )
   {
   // Find the top N pairs based on the values and return
   // as a dynamically-allocated array of pointers.  If there
   // are less than N pairs in the hash, remaining pointers
   // will be null.

   // YOUR CODE HERE
    
    // need a comparator, greater
    // bool ( *greater ) ( const Value, const Value);
    
        // Create the dynamic allocated array, initialized with nullptr
        Pair** result = new Pair[N];
        for (int i = 0; i < N; ++i) {
            result[i] = nullptr;
        }
        int count = 0;
    
        // Go through the HashTable and insert all elements in the TopN array
        for (size_t i = 0; i < hashtable->numberOfBuckets; ++i) {
            Bucket b = hashtable->buckets[i];
            while (b != nullptr) {
                if (count < N) {
                    int j = count;
                    // directly insert the ptr of tuple
                    result[j] = b->tuple;
                    count++;
                    // should sort every time
                    while (j > 0 && greater(result[j]->Value, result[j - 1]->Value)) {
                        // swap (avoid STL)
                        Pair* tmp = result[j - 1];
                        result[j - 1] = result[j];
                        result[j] = tmp;
                        j--;
                    }
                } else {
                    // do comparision of the values
                    // if greater, push_back at the end the array, then shifting
                    // elements to keep the array sorted in descending order, to
                    // avoid unnecessary shiftings
                    int j = N - 1;
                    if (greater(b->tuple->Value, result[j]->Value)) {
                        result[j] = b->tuple;
                        while (j > 0 && greater(result[j]->Value, result[j - 1]->Value)) {
                            Pair* tmp = result[j - 1];
                            result[j - 1] = result[j];
                            result[j] = tmp;
                            j--;
                        }
                        
                    }
                }
                b = b->next;
            }
        }
    
    
   return result;
   }

