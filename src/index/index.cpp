#include "index.h"

#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "blob_format.h"
#include "Common.h"

bool Index::PostingListView::findCheckpoint(int target_absolute_pos,
                                             int& out_absolute_pos,
                                             int& out_post_index) const {
  int lo = 0;
  int hi = static_cast<int>(seek_absolutes.size()) - 1;
  int best = -1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (seek_absolutes[mid] <= target_absolute_pos) {
      best = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  if (best == -1) {
    out_absolute_pos = 0;
    out_post_index = 0;
    return false;
  }
  out_absolute_pos = seek_absolutes[best];
  out_post_index = seek_indices[best];
  return true;
}

Index::Index() = default;

Index::Index(const std::string& path) { LoadBlob(path); }

Index::~Index() { close_mmap(); }

Index::Index(Index&& other) noexcept
    : mapped(other.mapped),
      mapped_size(other.mapped_size),
      base(other.base),
      n_docs(other.n_docs),
      n_terms(other.n_terms),
      doc_table_offset(other.doc_table_offset),
      title_refs_offset(other.title_refs_offset),
      dict_offset(other.dict_offset),
      posting_arena_offset(other.posting_arena_offset),
      string_arena_offset(other.string_arena_offset) {
  other.mapped = nullptr;
  other.mapped_size = 0;
  other.base = nullptr;
}

Index& Index::operator=(Index&& other) noexcept {
  if (this != &other) {
    close_mmap();
    mapped = other.mapped;
    mapped_size = other.mapped_size;
    base = other.base;
    n_docs = other.n_docs;
    n_terms = other.n_terms;
    doc_table_offset = other.doc_table_offset;
    title_refs_offset = other.title_refs_offset;
    dict_offset = other.dict_offset;
    posting_arena_offset = other.posting_arena_offset;
    string_arena_offset = other.string_arena_offset;
    other.mapped = nullptr;
    other.mapped_size = 0;
    other.base = nullptr;
  }
  return *this;
}

void Index::close_mmap() {
  if (mapped) {
    munmap(mapped, mapped_size);
    mapped = nullptr;
    mapped_size = 0;
    base = nullptr;
  }
}

char Index::decodeDecoration(uint32_t post) const {
  switch (post & 0x7) {
    case 0: return 'b';
    case 1: return '@';
    case 2: return '#';
    case 3: return '$';
    case 4: return '%';
    default: return 'x';
  }
}

bool Index::LoadBlob(const std::string& path) {
  close_mmap();

  int fd = open(path.c_str(), O_RDONLY);
  if (fd == -1) return false;

  struct stat st;
  if (fstat(fd, &st) == -1) { close(fd); return false; }
  mapped_size = static_cast<size_t>(st.st_size);
  if (mapped_size < sizeof(blob_v4::Header)) { close(fd); return false; }

  mapped = mmap(nullptr, mapped_size, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  if (mapped == MAP_FAILED) {
    mapped = nullptr;
    mapped_size = 0;
    return false;
  }

  base = reinterpret_cast<const char*>(mapped);
  const blob_v4::Header* hdr = reinterpret_cast<const blob_v4::Header*>(base);
  if (hdr->magic != blob_v4::MAGIC || hdr->version != blob_v4::VERSION) {
    close_mmap();
    return false;
  }

  n_docs = hdr->n_docs;
  n_terms = hdr->n_terms;
  doc_table_offset = hdr->doc_table_offset;
  title_refs_offset = hdr->title_refs_offset;
  dict_offset = hdr->dict_offset;
  posting_arena_offset = hdr->posting_arena_offset;
  string_arena_offset = hdr->string_arena_offset;
  return true;
}

// dictionary lookup 

uint64_t Index::find_term_offset(const std::string& term) const {
  if (!mapped) return 0;

  const char* dict = at(dict_offset);
  uint64_t n_buckets;
  std::memcpy(&n_buckets, dict, sizeof(uint64_t));
  const uint64_t* bucket_offsets =
      reinterpret_cast<const uint64_t*>(dict + sizeof(uint64_t));

  uint64_t hash = str_hash(term);
  uint64_t off = bucket_offsets[hash % n_buckets];
  if (off == 0) return 0;

  // walk the chain. each tuple: uint64 length, uint64 posting_offset,
  // uint64 hash, char term[]; chain ends at a tuple whose length == 0
  while (true) {
    const blob_v4::SerialTuple* st =
        reinterpret_cast<const blob_v4::SerialTuple*>(at(off));
    if (st->length == 0) return 0;
    const char* st_term =
        reinterpret_cast<const char*>(st) + sizeof(blob_v4::SerialTuple);
    if (st->hash == hash && std::strcmp(st_term, term.c_str()) == 0) {
      return st->posting_list_offset;
    }
    off += st->length;
  }
}

Index::PostingListView Index::make_view_at(uint64_t posting_offset) const {
  PostingListView v;
  const blob_v4::PostingHeader* ph =
      reinterpret_cast<const blob_v4::PostingHeader*>(at(posting_offset));
  v.last_abs_pos = ph->last_abs_pos;
  v.num_docs = ph->num_docs;
  v.word_occurrences = ph->word_occurrences;
  v.last_doc_id = ph->last_doc_id;

  const char* after_header =
      at(posting_offset) + sizeof(blob_v4::PostingHeader);
  const uint32_t* posts = reinterpret_cast<const uint32_t*>(after_header);
  const int32_t*  seeks_abs = reinterpret_cast<const int32_t*>(
      after_header + ph->n_posts * sizeof(uint32_t));
  const int32_t*  seeks_idx = reinterpret_cast<const int32_t*>(
      after_header + ph->n_posts * sizeof(uint32_t)
                   + ph->n_checkpoints * sizeof(int32_t));

  v.posts = {posts, ph->n_posts};
  v.seek_absolutes = {seeks_abs, ph->n_checkpoints};
  v.seek_indices = {seeks_idx, ph->n_checkpoints};
  v.is_valid = true;
  return v;
}

Index::PostingListView Index::getPostingList(const std::string& term) const {
  uint64_t off = find_term_offset(term);
  if (off == 0) return PostingListView{}; // is_valid = false
  return make_view_at(off);
}

int Index::GetDocumentFrequency(const std::string& term) const {
  uint64_t off = find_term_offset(term);
  if (off == 0) return 0;
  const blob_v4::PostingHeader* ph =
      reinterpret_cast<const blob_v4::PostingHeader*>(at(off));
  return ph->num_docs;
}

// document accessors

const blob_v4::DocRecord* Index_DocRecord(const Index& /*ix*/,
                                           const char* base,
                                           uint64_t doc_table_offset,
                                           int docId) {
  return reinterpret_cast<const blob_v4::DocRecord*>(
      base + doc_table_offset + docId * sizeof(blob_v4::DocRecord));
}

Index::DocumentMetadata Index::GetDocumentMetadata(int docId) const {
  DocumentMetadata m;
  if (!mapped || docId < 0 || docId >= static_cast<int>(n_docs)) return m;

  const blob_v4::DocRecord* r = Index_DocRecord(*this, base, doc_table_offset, docId);
  m.doc_id = r->doc_id;
  m.hop_distance = r->hop_distance;
  m.body_length = r->body_length;
  m.start_position = r->start_position;
  m.end_position = r->end_position;
  m.page_file_rank = r->page_file_rank;
  m.page_file_num = r->page_file_num;
  m.page_file_index = r->page_file_index;

  // url lives in the string arena
  const char* strings = at(string_arena_offset);
  m.url.assign(strings + r->url_offset, r->url_length);

  // title words are a run of TitleWordRefs; each points back into string arena
  const blob_v4::TitleWordRef* refs =
      reinterpret_cast<const blob_v4::TitleWordRef*>(at(title_refs_offset));
  m.title_words.reserve(r->title_words_count);
  for (uint32_t i = 0; i < r->title_words_count; i++) {
    const auto& ref = refs[r->title_words_offset + i];
    m.title_words.emplace_back(strings + ref.string_offset, ref.length);
  }
  return m;
}

int Index::GetBodyLength(int docId) const {
  if (!mapped || docId < 0 || docId >= static_cast<int>(n_docs)) return 0;
  const blob_v4::DocRecord* r = Index_DocRecord(*this, base, doc_table_offset, docId);
  return r->body_length;
}

bool Index::GetDocumentRange(int docId, int& out_start, int& out_end) const {
  if (!mapped || docId < 0 || docId >= static_cast<int>(n_docs)) return false;
  const blob_v4::DocRecord* r = Index_DocRecord(*this, base, doc_table_offset, docId);
  out_start = r->start_position;
  out_end = r->end_position;
  return true;
}

// per-doc field queries

namespace {

// walk a PostingListView's posts within [start, end) absolute positions,
// invoking `fn(abs_pos, post)` for each post whose decoration matches
template <typename Fn>
void walk_field_posts(const Index::PostingListView& view, int start, int end,
                      char decoration, Fn fn) {
  if (view.posts.empty()) return;

  int cp_abs = 0, cp_idx = 0;
  bool has_cp = view.findCheckpoint(start, cp_abs, cp_idx);

  int idx;
  int abs_pos;
  if (has_cp) {
    idx = cp_idx;
    abs_pos = cp_abs;
  } else {
    idx = 0;
    abs_pos = static_cast<int>(view.posts[0] >> 3);
  }

  auto dec = [](uint32_t p) -> char {
    switch (p & 0x7) {
      case 0: return 'b';
      case 1: return '@';
      case 2: return '#';
      case 3: return '$';
      case 4: return '%';
      default: return 'x';
    }
  };

  while (idx < static_cast<int>(view.posts.size()) && abs_pos < end) {
    if (abs_pos >= start && dec(view.posts[idx]) == decoration) {
      fn(abs_pos, view.posts[idx]);
    }
    idx++;
    if (idx < static_cast<int>(view.posts.size())) {
      abs_pos += static_cast<int>(view.posts[idx] >> 3);
    }
  }
}

}  // namespace

int Index::GetFieldTermFrequency(int docId, const std::string& term, char decoration) const {
  if (!mapped || docId < 0 || docId >= static_cast<int>(n_docs)) return 0;
  PostingListView v = getPostingList(term);
  if (!v.is_valid) return 0;
  const blob_v4::DocRecord* r = Index_DocRecord(*this, base, doc_table_offset, docId);
  int count = 0;
  walk_field_posts(v, r->start_position, r->end_position, decoration,
                   [&](int, uint32_t) { count++; });
  return count;
}

std::vector<size_t> Index::GetFieldPositions(int docId, const std::string& term,
                                              char decoration) const {
  std::vector<size_t> out;
  if (!mapped || docId < 0 || docId >= static_cast<int>(n_docs)) return out;
  PostingListView v = getPostingList(term);
  if (!v.is_valid) return out;
  const blob_v4::DocRecord* r = Index_DocRecord(*this, base, doc_table_offset, docId);
  walk_field_posts(v, r->start_position, r->end_position, decoration,
                   [&](int abs_pos, uint32_t) { out.push_back(static_cast<size_t>(abs_pos)); });
  return out;
}
