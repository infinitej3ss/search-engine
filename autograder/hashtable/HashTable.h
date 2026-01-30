// Simple hash table template.

// Nicole Hamilton  nham@umich.edu

#pragma once

#include <cassert>
#include <iostream>
#include <iomanip>
#include <cstdint>

#include <cmath> // for ceil

using namespace std;


// You may add additional members or helper functions.


template< typename Key, typename Value > class Tuple
   {
   public:
      Key key;
      Value value;

      Tuple( const Key &k, const Value v ) : key( k ), value( v )
         {
         }
   };

template< typename Key, typename Value > class Bucket
   {
   public:
      Bucket *next;
      uint64_t hashValue;
      Tuple< Key, Value > tuple;

      Bucket( const Key &k, uint64_t h, const Value v ) :
            tuple( k, v ), next( nullptr ), hashValue( h )
         {
         }
   };


template< typename Key, typename Value > class HashTable
   {
   private:

      Bucket< Key, Value > **buckets;
      size_t numberOfBuckets;
      uint64_t ( *hash )( const Key );
      // YOUR CODE HERE
      bool (*compareEqual)(const Key, const Key);
       
      size_t uniqueKeys;

      friend class Iterator;
      friend class HashBlob;

      // YOUR CODE HERE


   public:

      Tuple< Key, Value > *Find( const Key k, const Value initialValue )
         {
         // Search for the key k and return a pointer to the
         // ( key, value ) entry.  If the key is not already
         // in the hash, add it with the initial value.

         // YOUR CODE HERE
              uint64_t h = hash(k);
              size_t idx = h % numberOfBuckets;
              Bucket<Key,Value>* b = buckets[idx];
              while (b != nullptr) {
                  if (compareEqual(b->tuple.key, k)) {
                      return &b->tuple;
                  }
                  b = b->next;
              }
              Bucket<Key,Value>* nb = new Bucket<Key,Value>(k, h, initialValue);
              // nb->next = buckets[idx], insertion at the front of the linked list;
              nb->next = buckets[idx];
              buckets[idx] = nb;
              uniqueKeys++;

             return &nb->tuple;
         }

      Tuple< Key, Value > *Find( const Key k ) const
         {
         // Search for the key k and return a pointer to the
         // ( key, value ) enty.  If the key is not already
         // in the hash, return nullptr.

         // YOUR CODE HERE
             uint64_t h = hash(k);
             size_t idx = h % numberOfBuckets;
             Bucket <Key,Value>* b = buckets[idx];
             while (b != nullptr) {
                 if (compareEqual(b->tuple.key, k)) {
                     return &b->tuple;
                 }
                 b = b->next;
             }
             return nullptr;
         }

      void Optimize( double loading = 1.5 )
         {
         // Grow or shrink the table as appropriate once we know the loading. A
         // goodrule of thumb is that the table size should be at least 1.5x the
         // number of unique keys.

            size_t target = static_cast<size_t>(
               ceil(uniqueKeys * loading)
            );

            if (!target) target = 1;

            Bucket<Key, Value> **new_buckets = new Bucket<Key, Value>*[target]();

            for (size_t i = 0; i < numberOfBuckets; ++i) {
               Bucket<Key, Value>* cur = buckets[i];
               while (cur) {
                  Bucket<Key, Value>* next = cur->next;

                  size_t idx = cur->hashValue % target;
                  cur->next = new_buckets[idx];
                  new_buckets[idx] = cur;

                  cur = next;
               }
            }

            delete[] buckets;
            buckets = new_buckets;
            
            numberOfBuckets = target;
         }


      // Your constructor may add defaults for arguments to the
      // constructor.  The compareEqual function should return
      // true if the keys are equal.  The hash function should
      // return a 64-bit value.

      HashTable( bool ( *compareEqual )( const Key, const Key ),
            uint64_t ( *hash )( const Key ),
            size_t numberOfBuckets ) : compareEqual(compareEqual), hash(hash), numberOfBuckets(numberOfBuckets), uniqueKeys(0)
         {
            buckets = new Bucket<Key, Value>*[numberOfBuckets]();
         }

      ~HashTable()
         {
            for (size_t i = 0; i < numberOfBuckets; ++i) {
               Bucket<Key, Value>* cur = buckets[i];
               while (cur) {
                  auto* next = cur->next;
                  delete cur;
                  cur = next;
               }
            }
            delete[] buckets;
         }

      class Iterator
         {
         private:

            friend class HashTable;

            // YOUR CODE HERE
            
            // needs to keep track of which table, the index of the bucket the head is at, and a pointer to that bucket
            HashTable *table= nullptr;
            size_t bucket= 0;
            Bucket<Key, Value> *b = nullptr;

            Iterator( HashTable *table, size_t bucket, Bucket<Key, Value> *b )
               {
                  this->table = table;
                  this->bucket = bucket;
                  this->b = b;
               }

         public:

            Iterator( ) : Iterator( nullptr, 0, nullptr )
               {
               }

            ~Iterator( )
               {
               }

            Tuple< Key, Value > &operator*( )
               {
                  return b->tuple;
               }

            Tuple< Key, Value > *operator->( ) const
               {
                  return &b->tuple;
               }

            // Prefix ++
            Iterator &operator++( )
               {
                  // caller handles safety
                  // more stuff in linked list
                  if (b->next) {
                     b = b->next;
                     return *this;
                  }

                  // nothing else in this bucket, move to next
                  bucket++;

                  // linear scan to next
                  while (bucket < table->numberOfBuckets && table->buckets[bucket] == nullptr) {
                     bucket++;
                  }

                  b = (bucket < table->numberOfBuckets) ? table->buckets[bucket] : nullptr;

                  return *this;
               }

            // Postfix ++
            Iterator operator++( int )
               {
                  Iterator tmp(*this);
                  operator++();
                  return tmp;
               }

            bool operator==( const Iterator &rhs ) const
               {
                  return (this->table == rhs.table && this->bucket == rhs.bucket && this->b == rhs.b);
               }

            bool operator!=( const Iterator &rhs ) const
               {
                  return !operator==(rhs);
               }
         };

      Iterator begin( )
         {
            // find first non-empty bucket (if exists)
            if (!uniqueKeys) return end();

            size_t i = 0;
            while (i < numberOfBuckets && buckets[i] == nullptr)++i;

            if (i == numberOfBuckets) return end();

            return Iterator(this, i, buckets[i]);
         }

      Iterator end( )
         {
            return Iterator(this, numberOfBuckets, nullptr);
         }
   };
