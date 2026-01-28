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


auto pair_ptr_left_bigger = [](Pair* lhs, Pair* rhs){ return lhs->value > rhs->value; };

Pair **TopN( Hash *hashtable, int N )
   {
   // Find the top N pairs based on the values and return
   // as a dynamically-allocated array of pointers.  If there
   // are less than N pairs in the hash, remaining pointers
   // will be null.

   // YOUR CODE HERE

   PQ<Pair*, decltype(pair_ptr_left_bigger)> pq(N, pair_ptr_left_bigger);
   auto head = hashtable->begin();
   auto end = hashtable->end();

   for (;;) {
      if (head == end) break;
      
      pq.push(&*head++);
   }

    Pair** result = new Pair*[N]();
    for (size_t i = 0; i < pq.size(); ++i) {
        result[i] = pq.get(i);
    }

   return nullptr;
   }

