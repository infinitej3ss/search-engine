#pragma once

// HashBlob, a serialization of a HashTable into one contiguous
// block of memory, possibly memory-mapped to a HashFile.

// Nicole Hamilton  nham@umich.edu

#include <memory>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <sys/mman.h>

#include "HashTable.h"


using Hash = HashTable< const char *, size_t >;
using Pair = Tuple< const char *, size_t >;
using HashBucket = Bucket< const char *, size_t >;


static const size_t Unknown = 0;


size_t RoundUp( size_t length, size_t boundary )
   {
   // Round up to the next multiple of the boundary, which
   // must be a power of 2.

   static const size_t oneless = boundary - 1,
      mask = ~( oneless );
   return ( length + oneless ) & mask;
   }


struct SerialTuple
   {
   // This is a serialization of a HashTable< char *, size_t >::Bucket.
   // One is packed up against the next in a HashBlob.

   // Since this struct includes size_t and uint64_t members, we'll
   // require that it be sizeof( size_t ) aligned to avoid unaligned
   // accesses.

   // Alternatives might be to use #pragma pack() and pay the perf
   // penalty or use a Utf8-style variable-length encoding that
   // does not need to be aligned.

   public:

      // SerialTupleLength = 0 is a sentinel indicating
      // this is the last SerialTuple chained in this list.
      // (Actual length is not given but not needed.)

      size_t Length, Value;
      uint64_t HashValue;

      // The Key will be a C-string of whatever length.
      char Key[ Unknown ];

      // Calculate the bytes required to encode a HashBucket as a
      // SerialTuple.

      static size_t BytesRequired(const HashBucket* b)
         {
         size_t st = sizeof(size_t);

         // overhead
         size_t fixed = st * 2 + sizeof(uint64_t); // Length, Value, HashValue

         size_t key_bytes = strlen(b->tuple.key) + 1; // include c string sentinel

         size_t total = fixed + key_bytes;

         return RoundUp(total, sizeof(size_t));
         }

      // Write the HashBucket out as a SerialTuple in the buffer,
      // returning a pointer to one past the last character written.

      static char *Write( char *buffer, char *bufferEnd,
            const HashBucket *b )
         {
         size_t size = BytesRequired(b);

         // safety
         if (buffer + size > bufferEnd) return nullptr; // don't know how to actually handle

         // load bucket in
         const Pair &data = b->tuple;

         // write serial
         // Length, Value, HashValue, Key
         std::memset(buffer, 0, size);
         char *p = buffer;

         // fixed fields
         std::memcpy(p, &size, sizeof(size_t)); p += sizeof(size_t);
         std::memcpy(p, &data.value,  sizeof(size_t)); p += sizeof(size_t);
         std::memcpy(p, &b->hashValue,   sizeof(uint64_t)); p += sizeof(uint64_t);

         // key bytes
         const char *key = data.key;
         size_t key_len = std::strlen(key) + 1;
         std::memcpy(p, key, key_len);
         p += key_len;

         return buffer + size;
         }
  };


class HashBlob
   {
   // This will be a hash specifically designed to hold an
   // entire hash table as a single contiguous blob of bytes.
   // Pointers are disallowed so that the blob can be
   // relocated to anywhere in memory

   // The basic structure should consist of some header
   // information including the number of buckets and other
   // details followed by a concatenated list of all the
   // individual lists of tuples within each bucket.

   public:

      // Define a MagicNumber and Version so you can validate
      // a HashBlob really is one of your HashBlobs.

      size_t MagicNumber,
         Version,
         BlobSize,
         NumberOfBuckets,
         Buckets[ Unknown ];

      // The SerialTuples will follow immediately after.

      const SerialTuple *Find( const char *key ) const
         {
         // Search for the key k and return a pointer to the
         // ( key, value ) entry.  If the key is not found,
         // return nullptr.

         // YOUR CODE HERE

         return nullptr;
         }


      static size_t BytesRequired( const Hash *hashTable )
         {
         // Calculate how much space it will take to
         // represent a HashTable as a HashBlob.

         // Need space for the header + buckets +
         // all the serialized tuples.

         // header
         size_t st = sizeof(size_t);
         size_t total = st * 4; // MagicNumber, Version, BlobSize, NumberOfBuckets
         
         total += hashTable->numberOfBuckets * st; // offsets

         // since hashTable entries are variable-length due to key, we have to check each bucket
         for (size_t i = 0; i < hashTable->numberOfBuckets; ++i) {
            SerialTuple a;
            HashBucket *cur = hashTable->buckets[i];
            if (cur) {
               while (cur) {
                  HashBucket *next = cur->next;
                  total += a.BytesRequired(cur);
                  cur = next;
               }
               total += st;
            }
         }

         return total;
         }

      // Write a HashBlob into a buffer, returning a
      // pointer to the blob.

      static HashBlob *Write( HashBlob *hb, size_t bytes,
            const Hash *hashTable )
         {
         // safety
         size_t size = BytesRequired(hashTable);

         // safety
         if (size > bytes) return nullptr; // don't know how to actually handle

         // write serial
         std::memset(hb, 0, bytes);
         char *p = reinterpret_cast<char*>(hb);

         // header
         size_t st = sizeof(size_t);
         size_t magic = 48105;
         size_t ver = 1;

         std::memcpy(p, &magic, st); p += st;
         std::memcpy(p, &ver, st); p += st;
         std::memcpy(p, &size, st); p += st;
         std::memcpy(p, &hashTable->numberOfBuckets, st); p+= st;

         // p is now the pointer for the bucket offsets
         // b is the pointer for the actual buckets
         char *b = p + hashTable->numberOfBuckets * st;
         SerialTuple a;

         for (size_t i = 0; i < hashTable->numberOfBuckets; ++i) {
            HashBucket* cur = hashTable->buckets[i];
            // current bucket
            size_t offset = b - reinterpret_cast<char*>(hb);
            memcpy(p, &offset, st);
            p += st;
            if (cur) {
               while (cur) {
                  HashBucket* next = cur->next;
                  size_t s = a.BytesRequired(cur);

                  b = a.Write(b, b + s, cur);

                  cur = next;
               }
               // sentinel
               size_t zero = 0;
               memcpy(b, &zero, st);
               b += st;
            }
         }

         return hb;
         }

      // Create allocates memory for a HashBlob of required size
      // and then converts the HashTable into a HashBlob.
      // Caller is responsible for discarding when done.

      // (No easy way to override the new operator to create a
      // variable sized object.)

      static HashBlob *Create( const Hash *hashTable )
         {
         // YOUR CODE HERE

         return nullptr;
         }

      // Discard

      static void Discard( HashBlob *blob )
         {
         // YOUR CODE HERE
         }
   };

class HashFile
   {
   private:

      HashBlob *blob;

      size_t FileSize( int f )
         {
         struct stat fileInfo;
         fstat( f, &fileInfo );
         return fileInfo.st_size;
         }

   public:

      const HashBlob *Blob( )
         {
         return blob;
         }

      HashFile( const char *filename )
         {
         // Open the file for reading, map it and check the header.
         // new a HashFile, fill in the filehandle and blob address.

         // YOUR CODE HERE
         }

      HashFile( const char *filename, const Hash *hashtable )
         {
         // Open the file for write, map it, write
         // the hashtable out as a HashBlob, and note
         // the blob address.

         // YOUR CODE HERE
         }

      ~HashFile( )
         {
         // YOUR CODE HERE
         }
   };
