#!/usr/bin/env python3

import glob
import mmap
import struct
import sys
import os

"""
for reference
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
"""

MAGIC = 0x494E444558424C42
VERSION = 4

HEADER_FMT = "<QQQqQQQQQQQ5Q"
POSTING_HEADER_FMT = "<IIiiii"


def read_blob(path):
    fd = os.open(path, os.O_RDONLY)
    try:
        size = os.fstat(fd).st_size
        if size < 128:
            return
        mm = mmap.mmap(fd, 0, access=mmap.ACCESS_READ)
    except Exception:
        os.close(fd)
        raise

    try:
        hdr = struct.unpack_from(HEADER_FMT, mm, 0)
        magic, version = hdr[0], hdr[1]
        if magic != MAGIC or version != VERSION:
            print(f"skipping {path}: bad magic/version", file=sys.stderr)
            return

        n_docs = hdr[4]
        n_terms = hdr[5] # noqa shut up ruff
        dict_offset = hdr[8]

        (n_buckets,) = struct.unpack_from("<Q", mm, dict_offset)
        bucket_offsets_start = dict_offset + 8

        visited = set()

        for b in range(n_buckets):
            (off,) = struct.unpack_from("<Q", mm, bucket_offsets_start + b * 8)
            if off == 0:
                continue

            cursor = off
            while cursor + 8 <= size:
                (length,) = struct.unpack_from("<Q", mm, cursor)
                if length == 0:
                    break

                if cursor in visited:
                    break
                visited.add(cursor)

                posting_off, _ = struct.unpack_from("<QQ", mm, cursor + 8)
                term_start = cursor + 24
                term_end = mm.find(b"\x00", term_start, cursor + length)
                if term_end == -1:
                    term_end = cursor + length
                term = mm[term_start:term_end].decode("utf-8", errors="replace")

                ph = struct.unpack_from(POSTING_HEADER_FMT, mm, posting_off)
                word_occurrences = ph[4]

                yield term, word_occurrences, n_docs

                cursor += length
    finally:
        mm.close()
        os.close(fd)


def main():
    if len(sys.argv) > 1:
        pattern = sys.argv[1]
    else:
        pattern = "index_rank_*.blob"

    blobs = sorted(glob.glob(pattern))
    if not blobs:
        print(f"no blobs matching '{pattern}'", file=sys.stderr)
        sys.exit(1)

    all_terms = {} # term -> total word_occurrences across blobs
    total_docs = 0
    total_occurrences = 0

    for path in blobs:
        rank_terms = 0
        rank_occ = 0
        rank_docs = 0
        for term, occ, n_docs in read_blob(path):
            rank_docs = n_docs
            all_terms[term] = all_terms.get(term, 0) + occ
            rank_occ += occ
            rank_terms += 1
        total_docs += rank_docs
        print(f"{os.path.basename(path):30s}  {rank_terms:>10,} terms  {rank_occ:>14,} occurrences  {rank_docs:>10,} docs")

    total_occurrences = sum(all_terms.values())

    print()
    print(f"blobs scanned: {len(blobs)}")
    print(f"documents: {total_docs:,}")
    print(f"unique terms: {len(all_terms):,}")
    print(f"total occurrences: {total_occurrences:,}")


if __name__ == "__main__":
    main()
