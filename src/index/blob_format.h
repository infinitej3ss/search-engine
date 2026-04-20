#pragma once

// V4 mmap-native index blob format.
// all offsets are measured from the start of the blob in bytes
// every section is 8-byte aligned so uint64 fields never straddle a boundary

#include <cstdint>

namespace blob_v4 {

constexpr uint64_t MAGIC   = 0x494E444558424C42ULL;  // "INDEXBLB"
constexpr uint64_t VERSION = 4;

// fixed-size blob header at file offset 0
struct Header {
    uint64_t magic;
    uint64_t version;
    uint64_t total_size;
    int64_t  global_position_counter;
    uint64_t n_docs;
    uint64_t n_terms;
    uint64_t doc_table_offset;
    uint64_t title_refs_offset;
    uint64_t dict_offset;
    uint64_t posting_arena_offset;
    uint64_t string_arena_offset;
    uint64_t reserved[5];
};
static_assert(sizeof(Header) == 128, "blob_v4::Header must be 128 bytes");

// fixed-size per-doc record in the doc table
struct DocRecord {
    int32_t  doc_id;
    int32_t  hop_distance;
    int32_t  body_length;
    int32_t  start_position;
    int32_t  end_position;
    int32_t  _pad0; // align the uint64 fields that follow
    uint64_t page_file_rank;
    uint64_t page_file_num;
    uint64_t page_file_index;
    uint32_t url_offset; // into string arena
    uint32_t url_length;
    uint32_t title_words_offset; // into title refs (units: TitleWordRef entries)
    uint32_t title_words_count;
};
static_assert(sizeof(DocRecord) == 64, "blob_v4::DocRecord must be 64 bytes");

// one entry per title word across all docs, packed in the title_refs section
struct TitleWordRef {
    uint32_t string_offset; // into string arena
    uint32_t length;
};
static_assert(sizeof(TitleWordRef) == 8, "blob_v4::TitleWordRef must be 8 bytes");

// dictionary section layout (mirrors HashBlob from the hashtable branch):
//
//   uint64_t n_buckets;
//   uint64_t bucket_offsets[n_buckets];  // 0 = empty bucket, else absolute blob offset
//   packed SerialTuple chains
//
// each chain is a run of SerialTuples terminated by a single 8-byte zero.
// (readers check the first uint64 of the "next" tuple; if it's 0, chain ends.)
//
// each SerialTuple is:
//
//   uint64_t length; // total bytes of this tuple incl. padding
//   uint64_t posting_list_offset; // absolute blob offset into posting arena
//   uint64_t hash; // precomputed term hash (lets us skip strcmp on hash mismatch)
//   char term[]; // null-terminated
//   (pad to 8-byte boundary; padding bytes are zero)
struct SerialTuple {
    uint64_t length;
    uint64_t posting_list_offset;
    uint64_t hash;
    // followed by: char term[]; zero-padded to 8-byte boundary
};

// posting list layout in the posting arena:
//
//   PostingHeader header;
//   uint32_t posts[header.n_posts];
//   int32_t  seek_absolutes[header.n_checkpoints];
//   int32_t  seek_indices[header.n_checkpoints];
//
// total bytes = 24 + 4*n_posts + 8*n_checkpoints, padded to 8 bytes
struct PostingHeader {
    uint32_t n_posts;
    uint32_t n_checkpoints;
    int32_t  last_abs_pos;
    int32_t  num_docs;
    int32_t  word_occurrences;
    int32_t  last_doc_id;
};
static_assert(sizeof(PostingHeader) == 24, "blob_v4::PostingHeader must be 24 bytes");

// round length up to the next multiple of boundary (boundary must be a power of 2)
inline uint64_t round_up(uint64_t length, uint64_t boundary) {
    return (length + boundary - 1) & ~(boundary - 1);
}

}  // namespace blob_v4
