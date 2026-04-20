#include "index_builder.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "blob_format.h"
#include "Common.h"
#include "page_data.h"

namespace {

// one seek checkpoint every N posts per posting list
// tunable: lower means faster Seek at the cost of more per-list metadata
// tests/test_index_seek_checkpoints.cpp pins the same value, keep them in sync
constexpr size_t CHECKPOINT_STRIDE = 128;

} // namespace

// starts with a generous bucket count, then grows dynamically via
// HashTable::Find's load-factor check. bigger initial = fewer early resizes
IndexBuilder::IndexBuilder()
    : dictionary(CompareEqual, str_hash, 1 << 20), globalPositionCounter(0) {}

IndexBuilder::~IndexBuilder() {}

IndexBuilder::PostingList* IndexBuilder::getPostingList(const std::string& term) {
  Tuple<std::string, PostingList>* t = dictionary.Find(term);
  if (!t) t = dictionary.Find(term, PostingList());
  return &t->value;
}

int IndexBuilder::GetDocumentCount() const {
  return static_cast<int>(documents.size());
}

int IndexBuilder::GetDocumentFrequency(const std::string& term) const {
  Tuple<std::string, PostingList>* t = dictionary.Find(term);
  return t ? t->value.num_docs : 0;
}

const IndexBuilder::DocumentMetadata& IndexBuilder::GetDocumentMetadata(int docId) const {
  return documents[docId];
}

void IndexBuilder::Finalize() {
  dictionary.Optimize(1.5);
}

void IndexBuilder::addPostDirect(PostingList* pl, char decoration, int docId) {
  if (pl->last_doc_id != docId) {
    pl->num_docs++;
    pl->last_doc_id = docId;
  }

  int delta = globalPositionCounter - pl->last_abs_pos;
  pl->last_abs_pos = globalPositionCounter;

  uint32_t encoded = encodePost(decoration, delta);
  pl->addPost(encoded);

  if (pl->posts.size() % CHECKPOINT_STRIDE == 0) {
    pl->addCheckpoint(pl->last_abs_pos);
  }

  if (decoration != '%') {
    pl->word_occurrences++;
  }

  globalPositionCounter++;
}

std::vector<std::string> IndexBuilder::splitURL(const std::string& url) {
  std::vector<std::string> parts;
  std::string current;
  for (char c : url) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      current += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (!current.empty()) {
      parts.push_back(current);
      current.clear();
    }
  }
  if (!current.empty()) parts.push_back(current);
  return parts;
}

void IndexBuilder::addDocument(const PageData& page) {
  int docId = static_cast<int>(documents.size());

  DocumentMetadata doc_data;
  doc_data.doc_id = docId;
  doc_data.url = page.url;
  doc_data.title_words = page.titlewords;
  doc_data.hop_distance = static_cast<int>(page.distance_from_seedlist);
  doc_data.body_length = static_cast<int>(page.words.size());
  doc_data.page_file_rank = page.page_file_rank;
  doc_data.page_file_num = page.page_file_num;
  doc_data.page_file_index = page.page_file_index;
  doc_data.start_position = globalPositionCounter;

  // per-doc cache maps term -> posting list pointer. first occurrence in
  // this doc pays one dictionary lookup; subsequent occurrences hit the
  // cache. cache keys also serve as the unique-term set for the EOD loop
  std::unordered_map<std::string, PostingList*> doc_cache;

  // reused buffer so case-folding doesn't allocate per word
  std::string fold_buf;
  auto fold_into = [&](const std::string& src) -> const std::string& {
    fold_buf.clear();
    fold_buf.reserve(src.size());
    for (char c : src) {
      fold_buf.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return fold_buf;
  };

  auto emit_post = [&](const std::string& term, char decoration) {
    PostingList* pl;
    auto it = doc_cache.find(term);
    if (it == doc_cache.end()) {
      pl = getPostingList(term);
      doc_cache.emplace(term, pl);
    } else {
      pl = it->second;
    }
    addPostDirect(pl, decoration, docId);
  };

  // URL parts are already lowercased by splitURL
  std::vector<std::string> urlParts = splitURL(page.url);
  for (const std::string& part : urlParts) emit_post(part, '#');

  for (const std::string& word : page.titlewords)  emit_post(fold_into(word), '@');
  for (const std::string& word : page.words)       emit_post(fold_into(word), 'b');
  for (const std::string& word : page.anchor_text) emit_post(fold_into(word), '$');

  doc_data.end_position = globalPositionCounter;

  // emit an EOD post for every unique term that appeared in this doc.
  // all EODs share the "one past the last word" slot, so we do not bump
  // the counter inside this loop
  for (auto& [term, pl] : doc_cache) {
    (void)term;
    int delta = globalPositionCounter - pl->last_abs_pos;
    pl->addPost(encodePost('%', delta));
    pl->last_abs_pos = globalPositionCounter;
    if (pl->posts.size() % CHECKPOINT_STRIDE == 0) {
      pl->addCheckpoint(pl->last_abs_pos);
    }
  }

  // reserve one slot between docs so position ranges are disjoint.
  // otherwise the next doc's first word would share a slot with this doc's
  // EOD, and doc-lookup binary searches could attribute words to wrong docs
  globalPositionCounter++;

  documents.push_back(std::move(doc_data));
}

// V4 mmap-native writer
// see src/index/blob_format.h. two passes: (1) measure sections and assign
// every variable-length entry its absolute blob offset, (2) fill a buffer
// and flush to disk
bool IndexBuilder::WriteBlobV4(const std::string& path) const {
  using namespace blob_v4;

  // gather entries
  struct DictEntry {
    std::string term;
    const PostingList* pl;
    uint64_t hash;
  };
  std::vector<DictEntry> entries;
  for (auto it = const_cast<HashTable<std::string, PostingList>&>(dictionary).begin();
       it != const_cast<HashTable<std::string, PostingList>&>(dictionary).end(); ++it) {
    entries.push_back({it->key, &it->value, str_hash(it->key)});
  }
  const uint64_t n_terms = entries.size();

  // pick dictionary bucket count: smallest power of 2 >= 2 * n_terms, min 16
  uint64_t n_buckets = 16;
  while (n_buckets < n_terms * 2) n_buckets *= 2;

  // group entries into chains keyed by hash % n_buckets
  std::vector<std::vector<size_t>> chains(n_buckets);
  for (size_t i = 0; i < entries.size(); i++) {
    chains[entries[i].hash % n_buckets].push_back(i);
  }

  // measure sections (pass 1)

  const uint64_t doc_table_size = documents.size() * sizeof(DocRecord);

  uint64_t title_refs_count = 0;
  for (const auto& d : documents) title_refs_count += d.title_words.size();
  const uint64_t title_refs_size = title_refs_count * sizeof(TitleWordRef);

  std::vector<uint64_t> tuple_sizes(entries.size());
  for (size_t i = 0; i < entries.size(); i++) {
    uint64_t raw = sizeof(SerialTuple) + entries[i].term.size() + 1;
    tuple_sizes[i] = round_up(raw, 8);
  }
  uint64_t dict_tuples_bytes = 0;
  size_t non_empty_buckets = 0;
  for (const auto& chain : chains) {
    if (chain.empty()) continue;
    non_empty_buckets++;
    for (size_t idx : chain) dict_tuples_bytes += tuple_sizes[idx];
  }
  const uint64_t dict_bucket_table_size = sizeof(uint64_t) + n_buckets * sizeof(uint64_t);
  const uint64_t dict_sentinel_bytes = non_empty_buckets * sizeof(uint64_t);
  const uint64_t dict_size =
      dict_bucket_table_size + dict_tuples_bytes + dict_sentinel_bytes;

  std::vector<uint64_t> posting_sizes(entries.size());
  for (size_t i = 0; i < entries.size(); i++) {
    const PostingList& pl = *entries[i].pl;
    uint64_t raw = sizeof(PostingHeader) + pl.posts.size() * sizeof(uint32_t)
                 + pl.seek_absolutes.size() * sizeof(int32_t)
                 + pl.seek_indices.size() * sizeof(int32_t);
    posting_sizes[i] = round_up(raw, 8);
  }
  uint64_t posting_arena_size = 0;
  for (uint64_t s : posting_sizes) posting_arena_size += s;

  std::vector<uint32_t> doc_url_offsets(documents.size());
  std::vector<uint32_t> doc_title_refs_starts(documents.size());
  std::vector<TitleWordRef> all_title_refs;
  all_title_refs.reserve(title_refs_count);
  std::vector<char> string_arena;

  for (size_t i = 0; i < documents.size(); i++) {
    const auto& d = documents[i];
    doc_url_offsets[i] = static_cast<uint32_t>(string_arena.size());
    string_arena.insert(string_arena.end(), d.url.begin(), d.url.end());

    doc_title_refs_starts[i] = static_cast<uint32_t>(all_title_refs.size());
    for (const auto& w : d.title_words) {
      TitleWordRef ref;
      ref.string_offset = static_cast<uint32_t>(string_arena.size());
      ref.length = static_cast<uint32_t>(w.size());
      all_title_refs.push_back(ref);
      string_arena.insert(string_arena.end(), w.begin(), w.end());
    }
  }
  const uint64_t string_arena_size = round_up(string_arena.size(), 8);

  // assign section offsets

  const uint64_t header_offset         = 0;
  const uint64_t doc_table_offset      = sizeof(Header);
  const uint64_t title_refs_offset     = doc_table_offset + doc_table_size;
  const uint64_t dict_offset           = title_refs_offset + title_refs_size;
  const uint64_t posting_arena_offset  = dict_offset + dict_size;
  const uint64_t string_arena_offset   = posting_arena_offset + posting_arena_size;
  const uint64_t total_size            = string_arena_offset + string_arena_size;

  std::vector<uint64_t> posting_abs_offsets(entries.size());
  {
    uint64_t cursor = posting_arena_offset;
    for (size_t i = 0; i < entries.size(); i++) {
      posting_abs_offsets[i] = cursor;
      cursor += posting_sizes[i];
    }
  }

  std::vector<uint64_t> tuple_abs_offsets(entries.size());
  std::vector<uint64_t> bucket_offsets(n_buckets, 0);
  {
    uint64_t cursor = dict_offset + dict_bucket_table_size;
    for (size_t b = 0; b < n_buckets; b++) {
      if (chains[b].empty()) continue;
      bucket_offsets[b] = cursor;
      for (size_t idx : chains[b]) {
        tuple_abs_offsets[idx] = cursor;
        cursor += tuple_sizes[idx];
      }
      cursor += sizeof(uint64_t);  // chain-end sentinel
    }
  }

  // write (pass 2)

  std::vector<char> buf(total_size, 0);

  Header hdr{};
  hdr.magic = MAGIC;
  hdr.version = VERSION;
  hdr.total_size = total_size;
  hdr.global_position_counter = static_cast<int64_t>(globalPositionCounter);
  hdr.n_docs = documents.size();
  hdr.n_terms = n_terms;
  hdr.doc_table_offset = doc_table_offset;
  hdr.title_refs_offset = title_refs_offset;
  hdr.dict_offset = dict_offset;
  hdr.posting_arena_offset = posting_arena_offset;
  hdr.string_arena_offset = string_arena_offset;
  std::memcpy(buf.data() + header_offset, &hdr, sizeof(hdr));

  for (size_t i = 0; i < documents.size(); i++) {
    const auto& d = documents[i];
    DocRecord rec{};
    rec.doc_id = d.doc_id;
    rec.hop_distance = d.hop_distance;
    rec.body_length = d.body_length;
    rec.start_position = d.start_position;
    rec.end_position = d.end_position;
    rec.page_file_rank = d.page_file_rank;
    rec.page_file_num = d.page_file_num;
    rec.page_file_index = d.page_file_index;
    rec.url_offset = doc_url_offsets[i];
    rec.url_length = static_cast<uint32_t>(d.url.size());
    rec.title_words_offset = doc_title_refs_starts[i];
    rec.title_words_count = static_cast<uint32_t>(d.title_words.size());
    std::memcpy(buf.data() + doc_table_offset + i * sizeof(DocRecord),
                &rec, sizeof(rec));
  }

  if (!all_title_refs.empty()) {
    std::memcpy(buf.data() + title_refs_offset, all_title_refs.data(),
                all_title_refs.size() * sizeof(TitleWordRef));
  }

  {
    char* p = buf.data() + dict_offset;
    std::memcpy(p, &n_buckets, sizeof(uint64_t));
    p += sizeof(uint64_t);
    std::memcpy(p, bucket_offsets.data(), n_buckets * sizeof(uint64_t));
  }
  for (size_t b = 0; b < n_buckets; b++) {
    if (chains[b].empty()) continue;
    for (size_t idx : chains[b]) {
      char* p = buf.data() + tuple_abs_offsets[idx];
      SerialTuple st{};
      st.length = tuple_sizes[idx];
      st.posting_list_offset = posting_abs_offsets[idx];
      st.hash = entries[idx].hash;
      std::memcpy(p, &st, sizeof(st));
      const std::string& term = entries[idx].term;
      std::memcpy(p + sizeof(SerialTuple), term.data(), term.size());
      // null terminator + padding already zero from the buffer's initial fill
    }
    // chain-end sentinel: 8 bytes of zero already in buf
  }

  for (size_t i = 0; i < entries.size(); i++) {
    const PostingList& pl = *entries[i].pl;
    char* p = buf.data() + posting_abs_offsets[i];
    PostingHeader ph{};
    ph.n_posts = static_cast<uint32_t>(pl.posts.size());
    ph.n_checkpoints = static_cast<uint32_t>(pl.seek_absolutes.size());
    ph.last_abs_pos = pl.last_abs_pos;
    ph.num_docs = pl.num_docs;
    ph.word_occurrences = pl.word_occurrences;
    ph.last_doc_id = pl.last_doc_id;
    std::memcpy(p, &ph, sizeof(ph));
    p += sizeof(ph);
    if (!pl.posts.empty()) {
      std::memcpy(p, pl.posts.data(), pl.posts.size() * sizeof(uint32_t));
      p += pl.posts.size() * sizeof(uint32_t);
    }
    if (!pl.seek_absolutes.empty()) {
      std::memcpy(p, pl.seek_absolutes.data(),
                  pl.seek_absolutes.size() * sizeof(int32_t));
      p += pl.seek_absolutes.size() * sizeof(int32_t);
    }
    if (!pl.seek_indices.empty()) {
      std::memcpy(p, pl.seek_indices.data(),
                  pl.seek_indices.size() * sizeof(int32_t));
    }
  }

  if (!string_arena.empty()) {
    std::memcpy(buf.data() + string_arena_offset,
                string_arena.data(), string_arena.size());
  }

  std::ofstream o(path, std::ios::binary | std::ios::trunc);
  if (!o) return false;
  o.write(buf.data(), static_cast<std::streamsize>(total_size));
  return static_cast<bool>(o);
}

IndexBuilder* BuildIndex() {
  auto* idx = new IndexBuilder();
  // (kept for API compat; build_index.cpp walks page files directly now)
  return idx;
}
