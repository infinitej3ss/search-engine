#pragma once

// build-time inverted index. used only during index construction by
// build_index and test setup. produces V4 blobs via WriteBlobV4; all
// query-time access goes through the mmap-backed Index class (index.h)

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "HashTable.h"
#include "page_data.h"

class IndexBuilder {
 public:
  // decoration bits in the low 3 of each post:
  //   'b' body (0), '@' title (1), '#' url (2), '$' anchor (3), '%' EOD (4)
  uint32_t encodePost(char decoration, int delta) const {
    uint32_t dec_bits = 0;
    switch (decoration) {
      case 'b': dec_bits = 0; break;
      case '@': dec_bits = 1; break;
      case '#': dec_bits = 2; break;
      case '$': dec_bits = 3; break;
      case '%': dec_bits = 4; break;
      default: break;
    }
    return (delta << 3) | dec_bits;
  }

  struct PostingList {
    std::vector<uint32_t> posts;
    std::vector<int> seek_absolutes;
    std::vector<int> seek_indices;
    int last_abs_pos = 0;
    int num_docs = 0;
    int word_occurrences = 0;
    int last_doc_id = -1;

    PostingList() = default;

    void addPost(uint32_t encoded) { posts.push_back(encoded); }

    void addCheckpoint(int absolute_pos) {
      seek_absolutes.push_back(absolute_pos);
      seek_indices.push_back(static_cast<int>(posts.size()) - 1);
    }
  };

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

  IndexBuilder();
  ~IndexBuilder();

  void addDocument(const PageData& page);
  void Finalize();

  // V4 mmap-native writer. produces the blob layout that mmap-backed Index
  // reads directly at query time (see src/index/blob_format.h)
  bool WriteBlobV4(const std::string& path) const;

  // accessors used by tests and during the migration to mmap-Index
  PostingList* getPostingList(const std::string& term);
  int GetDocumentCount() const;
  int GetDocumentFrequency(const std::string& term) const;
  const DocumentMetadata& GetDocumentMetadata(int docId) const;

 private:
  HashTable<std::string, PostingList> dictionary;
  std::vector<DocumentMetadata> documents;
  int globalPositionCounter = 0;

  // append a post without a dictionary lookup; used with the per-doc
  // posting-list cache in addDocument so repeated occurrences of the same
  // term don't re-hash the dictionary
  void addPostDirect(PostingList* pl, char decoration, int docId);

  static std::vector<std::string> splitURL(const std::string& url);
};

// read crawler page files `crawled_page_data_rank_<r>_num_<n>` from cwd and
// append them to an IndexBuilder. currently unused — build_index.cpp walks
// page files directly. kept for potential future use
IndexBuilder* BuildIndex();
