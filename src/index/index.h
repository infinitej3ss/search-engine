#pragma once

// read-only, mmap-backed inverted index. constructed from a V4 blob (see
// blob_format.h). zero-copy at query time: posting lists live inside the
// mapped region and are surfaced as non-owning views
//
// build-time index construction lives in IndexBuilder (index_builder.h)

#include <cstdint>
#include <span>
#include <string>
#include <sys/types.h>
#include <vector>

class Index {
 public:
  // non-owning view over one posting list. fields mirror the old in-memory
  // PostingList so ISR / constraint_solver keep working with minimal churn
  // (std::span supports .size() and operator[] like std::vector does)
  struct PostingListView {
    std::span<const uint32_t> posts;
    std::span<const int32_t>  seek_absolutes;
    std::span<const int32_t>  seek_indices;
    int32_t last_abs_pos = 0;
    int32_t num_docs = 0;
    int32_t word_occurrences = 0;
    int32_t last_doc_id = -1;
    bool is_valid = false;

    // binary search the checkpoint table for target_absolute_pos.
    // returns true and fills out_* if a checkpoint <= target was found
    bool findCheckpoint(int target_absolute_pos,
                         int& out_absolute_pos,
                         int& out_post_index) const;
  };

  // transition alias — old code that says `Index::PostingList` still compiles
  using PostingList = PostingListView;

  // per-doc metadata, materialized lazily from the mmap region
  struct DocumentMetadata {
    int doc_id = 0;
    std::string url;
    std::vector<std::string> title_words;
    int hop_distance = -1;
    int body_length = 0;
    int start_position = 0;
    int end_position = 0;
    u_int64_t page_file_rank = 0;
    u_int64_t page_file_num = 0;
    u_int64_t page_file_index = 0;
  };

  Index();
  explicit Index(const std::string& path);
  Index(const Index&) = delete;
  Index& operator=(const Index&) = delete;
  Index(Index&& other) noexcept;
  Index& operator=(Index&& other) noexcept;
  ~Index();

  bool isOpen() const { return mapped != nullptr; }

  // load a V4 blob via mmap. returns false if the file is missing, the
  // header fails magic/version checks, or mmap fails
  bool LoadBlob(const std::string& path);

  uint32_t decodeDelta(uint32_t post) const { return post >> 3; }
  char decodeDecoration(uint32_t post) const;

  int GetDocumentCount() const { return static_cast<int>(n_docs); }
  int GetDocumentFrequency(const std::string& term) const;
  DocumentMetadata GetDocumentMetadata(int docId) const;
  int GetBodyLength(int docId) const;

  // returns a view. use .is_valid on the returned view to check whether
  // the term was found. the view's spans are valid for the lifetime of
  // this Index object
  PostingListView getPostingList(const std::string& term) const;

  int GetFieldTermFrequency(int docId, const std::string& term, char decoration) const;
  std::vector<size_t> GetFieldPositions(int docId, const std::string& term, char decoration) const;

  // lightweight per-doc range lookup used by ISR::GetCurrentDocId's binary
  // search. avoids materializing the full DocumentMetadata (url, title
  // words) on every step
  bool GetDocumentRange(int docId, int& out_start, int& out_end) const;

 private:
  void close_mmap();

  // blob offset → pointer into the mapped region
  const char* at(uint64_t offset) const { return base + offset; }

  // walk the dictionary chain for `term`; returns the posting list's
  // absolute blob offset, or 0 if the term isn't in the dictionary
  uint64_t find_term_offset(const std::string& term) const;

  // construct a PostingListView from a posting list's absolute blob offset
  PostingListView make_view_at(uint64_t posting_offset) const;

  void* mapped = nullptr;
  size_t mapped_size = 0;
  const char* base = nullptr;

  // cached from the blob header
  uint64_t n_docs = 0;
  uint64_t n_terms = 0;
  uint64_t doc_table_offset = 0;
  uint64_t title_refs_offset = 0;
  uint64_t dict_offset = 0;
  uint64_t posting_arena_offset = 0;
  uint64_t string_arena_offset = 0;
};
