#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "blob_format.h"
#include "Common.h"
#include "index_builder.h"
#include "page_data.h"

// sanity tests for the V4 blob writer. the reader is landing in the next
// phase; until then these tests validate the on-disk layout by walking it
// manually via blob_format.h structs

namespace {

PageData make_page(const std::string& url,
                   const std::vector<std::string>& title,
                   const std::vector<std::string>& body) {
  PageData p;
  p.url = url;
  p.titlewords = title;
  p.words = body;
  p.anchor_text = {};
  p.distance_from_seedlist = 1;
  return p;
}

std::vector<char> slurp(const std::string& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  std::streamsize size = f.tellg();
  f.seekg(0);
  std::vector<char> buf(size);
  f.read(buf.data(), size);
  return buf;
}

// look up a term in the V4 dict section and return its posting list offset,
// or 0 if not found. mirrors HashBlob::Find's walk logic
uint64_t find_term(const char* blob, uint64_t dict_offset, const std::string& term) {
  const char* dict = blob + dict_offset;
  uint64_t n_buckets;
  std::memcpy(&n_buckets, dict, sizeof(uint64_t));
  const uint64_t* bucket_offsets =
      reinterpret_cast<const uint64_t*>(dict + sizeof(uint64_t));

  uint64_t hash = str_hash(term);
  uint64_t off = bucket_offsets[hash % n_buckets];
  if (off == 0) return 0;

  while (true) {
    const blob_v4::SerialTuple* st =
        reinterpret_cast<const blob_v4::SerialTuple*>(blob + off);
    if (st->length == 0) return 0;
    const char* st_term = reinterpret_cast<const char*>(st) + sizeof(blob_v4::SerialTuple);
    if (st->hash == hash && std::strcmp(st_term, term.c_str()) == 0) {
      return st->posting_list_offset;
    }
    off += st->length;
  }
}

}  // namespace

TEST_CASE("V4 writer produces a valid header", "[blob_v4][writer]") {
  IndexBuilder idx;
  idx.addDocument(make_page("https://ex.com/a", {"alpha"}, {"one", "two"}));
  idx.addDocument(make_page("https://ex.com/b", {"beta"}, {"three"}));
  idx.Finalize();

  const std::string path = "test_blob_v4_header.blob";
  REQUIRE(idx.WriteBlobV4(path));

  auto buf = slurp(path);
  REQUIRE(buf.size() >= sizeof(blob_v4::Header));

  const blob_v4::Header* hdr = reinterpret_cast<const blob_v4::Header*>(buf.data());
  REQUIRE(hdr->magic == blob_v4::MAGIC);
  REQUIRE(hdr->version == 4);
  REQUIRE(hdr->n_docs == 2);
  REQUIRE(hdr->n_terms > 0);
  REQUIRE(hdr->total_size == buf.size());

  // sections should be monotonically increasing and non-overlapping
  REQUIRE(hdr->doc_table_offset == sizeof(blob_v4::Header));
  REQUIRE(hdr->title_refs_offset > hdr->doc_table_offset);
  REQUIRE(hdr->dict_offset > hdr->title_refs_offset);
  REQUIRE(hdr->posting_arena_offset > hdr->dict_offset);
  REQUIRE(hdr->string_arena_offset > hdr->posting_arena_offset);
  REQUIRE(hdr->total_size >= hdr->string_arena_offset);

  std::remove(path.c_str());
}

TEST_CASE("V4 writer's doc table records match addDocument input",
          "[blob_v4][writer]") {
  IndexBuilder idx;
  idx.addDocument(make_page("https://ex.com/a", {"alpha", "page"}, {"one", "two", "three"}));
  idx.addDocument(make_page("https://ex.com/b", {"beta"}, {"four"}));
  idx.Finalize();

  const std::string path = "test_blob_v4_docs.blob";
  REQUIRE(idx.WriteBlobV4(path));

  auto buf = slurp(path);
  const blob_v4::Header* hdr = reinterpret_cast<const blob_v4::Header*>(buf.data());
  const blob_v4::DocRecord* docs = reinterpret_cast<const blob_v4::DocRecord*>(
      buf.data() + hdr->doc_table_offset);

  REQUIRE(docs[0].doc_id == 0);
  REQUIRE(docs[0].body_length == 3);
  REQUIRE(docs[0].url_length == std::strlen("https://ex.com/a"));
  REQUIRE(docs[0].title_words_count == 2);

  REQUIRE(docs[1].doc_id == 1);
  REQUIRE(docs[1].body_length == 1);
  REQUIRE(docs[1].title_words_count == 1);

  // urls in string arena should match
  const char* strings = buf.data() + hdr->string_arena_offset;
  std::string url0(strings + docs[0].url_offset, docs[0].url_length);
  REQUIRE(url0 == "https://ex.com/a");

  std::remove(path.c_str());
}

TEST_CASE("V4 writer dict find returns a valid posting list offset",
          "[blob_v4][writer]") {
  IndexBuilder idx;
  idx.addDocument(make_page("https://ex.com/a", {"t"}, {"foo", "bar", "foo"}));
  idx.Finalize();

  const std::string path = "test_blob_v4_dict.blob";
  REQUIRE(idx.WriteBlobV4(path));

  auto buf = slurp(path);
  const blob_v4::Header* hdr = reinterpret_cast<const blob_v4::Header*>(buf.data());

  uint64_t foo_off = find_term(buf.data(), hdr->dict_offset, "foo");
  uint64_t bar_off = find_term(buf.data(), hdr->dict_offset, "bar");
  uint64_t miss_off = find_term(buf.data(), hdr->dict_offset, "nonexistent");

  REQUIRE(foo_off >= hdr->posting_arena_offset);
  REQUIRE(foo_off < hdr->string_arena_offset);
  REQUIRE(bar_off >= hdr->posting_arena_offset);
  REQUIRE(bar_off < hdr->string_arena_offset);
  REQUIRE(miss_off == 0);

  // sanity check posting list header for "foo": 2 occurrences in body
  const blob_v4::PostingHeader* ph =
      reinterpret_cast<const blob_v4::PostingHeader*>(buf.data() + foo_off);
  REQUIRE(ph->word_occurrences == 2);  // "foo" appears twice in body
  REQUIRE(ph->num_docs == 1);
  REQUIRE(ph->n_posts > 0);

  std::remove(path.c_str());
}

TEST_CASE("V4 writer handles an empty index", "[blob_v4][writer]") {
  IndexBuilder idx;
  idx.Finalize();

  const std::string path = "test_blob_v4_empty.blob";
  REQUIRE(idx.WriteBlobV4(path));

  auto buf = slurp(path);
  const blob_v4::Header* hdr = reinterpret_cast<const blob_v4::Header*>(buf.data());
  REQUIRE(hdr->magic == blob_v4::MAGIC);
  REQUIRE(hdr->n_docs == 0);
  REQUIRE(hdr->n_terms == 0);

  std::remove(path.c_str());
}
