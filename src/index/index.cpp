#include "index.h"
#include "Common.h"
#include "page_data.h"
#include <cctype>
#include <iostream>
#include <fstream>
#include <set>
#include <cstdint>

using namespace std;

namespace {
// one seek checkpoint every N posts per posting list
// tunable: lower means faster Seek at the cost of more per-list metadata
// tests/test_index_seek_checkpoints.cpp pins the same value, keep them in sync
constexpr size_t CHECKPOINT_STRIDE = 128;

// case-fold a token for dictionary storage. query compiler always lowercases,
// so dictionary keys must match
std::string fold(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

// starts with a generous bucket count because Optimize only runs in Finalize/LoadBlob
// during build, every insertion hits a chain, so under-sized = slow build
Index::Index() : dictionary(CompareEqual, str_hash, 1 << 20), globalPositionCounter(0) {}

Index::~Index() {}

Index::PostingList* Index::getPostingList(const string& term){
    Tuple<std::string, PostingList>* t = dictionary.Find(term);
    if (!t) {
        t = dictionary.Find(term, PostingList());
    }
    return &t->value;
}

void Index::addPost(const std::string& term, char decoration, int docId){
    PostingList* pl = getPostingList(term);

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


void Index::addDocument(const PageData& page) {
    int docId = (int)documents.size();
    std::set<std::string> uniqueWords;

    DocumentMetadata doc_data;
    doc_data.doc_id = docId;
    doc_data.url = page.url.c_str();
    doc_data.title_words = page.titlewords;
    doc_data.hop_distance = static_cast<int>(page.distance_from_seedlist);
    doc_data.body_length = (int)page.words.size();
    doc_data.page_file_rank = page.page_file_rank;
    doc_data.page_file_num = page.page_file_num;
    doc_data.page_file_index = page.page_file_index;
    doc_data.start_position = globalPositionCounter;

    std::vector<std::string> urlParts = splitURL(page.url);
    for (const std::string& part : urlParts) {
        uniqueWords.insert(part);
        addPost(part, '#', docId);
    }

    for (const string& word : page.titlewords) {
        std::string w = fold(word);
        uniqueWords.insert(w);
        addPost(w, '@', docId);
    }

    for (const string& word : page.words) {
        std::string w = fold(word);
        uniqueWords.insert(w);
        addPost(w, 'b', docId);
    }

    for (const string& word : page.anchor_text) {
        std::string w = fold(word);
        uniqueWords.insert(w);
        addPost(w, '$', docId);
    }

    doc_data.end_position = globalPositionCounter;

    // emit an EOD post for every term that appeared in this doc. all EODs share
    // the "one past the last word" slot, so we do not bump the counter inside
    // this loop
    for (const std::string& term : uniqueWords) {
        PostingList* pl = getPostingList(term);
        int delta = globalPositionCounter - pl->last_abs_pos;
        pl->addPost(encodePost('%', delta));
        pl->last_abs_pos = globalPositionCounter;
        if (pl->posts.size() % CHECKPOINT_STRIDE == 0) {
            pl->addCheckpoint(pl->last_abs_pos);
        }
    }

    // reserve one slot between docs so position ranges are disjoint
    // otherwise the next doc's first word would share a slot with this doc's
    // EOD, and ISR::GetCurrentDocId's binary search could attribute a word to
    // the wrong doc
    globalPositionCounter++;

    documents.push_back(doc_data);
}

Index::DocumentMetadata Index::GetDocumentMetadata(int docId){
    return documents[docId];
}

int Index::GetDocumentCount() const{
    return (int)documents.size();
}

int Index::GetDocumentFrequency(const string& term) const {
    Tuple<std::string, PostingList>* t = dictionary.Find(term);
    if (!t) return 0;
    return t->value.num_docs;
}

int Index::GetBodyLength(int docId) const {
    if (docId < 0 || docId >= (int)documents.size()) return 0;
    return documents[docId].body_length;
}

namespace {
// walk `pl`'s posts within [start, end) in absolute-position space, invoking
// `fn(abs_pos, post)` for each post whose decoration matches `decoration`.
// uses checkpoints when available to skip the prefix
template <typename Fn>
void walk_field_posts(const Index::PostingList& pl, int start, int end,
                      char decoration, char (*decode_dec)(uint32_t),
                      uint32_t (*decode_delta)(uint32_t), Fn fn) {
    if (pl.posts.empty()) return;

    int cp_abs = 0, cp_idx = 0;
    bool has_cp = pl.findCheckpoint(start, cp_abs, cp_idx);

    int idx;
    int abs_pos;
    if (has_cp) {
        idx = cp_idx;
        abs_pos = cp_abs;
    } else {
        idx = 0;
        abs_pos = (int)decode_delta(pl.posts[0]);
    }

    while (idx < (int)pl.posts.size() && abs_pos < end) {
        if (abs_pos >= start && decode_dec(pl.posts[idx]) == decoration) {
            fn(abs_pos, pl.posts[idx]);
        }
        idx++;
        if (idx < (int)pl.posts.size()) {
            abs_pos += (int)decode_delta(pl.posts[idx]);
        }
    }
}
}  // namespace

int Index::GetFieldTermFrequency(int docId, const std::string& term, char decoration) const {
    if (docId < 0 || docId >= (int)documents.size()) return 0;
    Tuple<std::string, PostingList>* t = dictionary.Find(term);
    if (!t) return 0;

    const auto& doc = documents[docId];
    int count = 0;
    auto dec = [](uint32_t p) -> char {
        uint32_t v = p & 0x7;
        switch (v) {
            case 0: return 'b';
            case 1: return '@';
            case 2: return '#';
            case 3: return '$';
            case 4: return '%';
            default: return 'x';
        }
    };
    auto del = [](uint32_t p) -> uint32_t { return p >> 3; };
    walk_field_posts(t->value, doc.start_position, doc.end_position, decoration,
                     dec, del, [&](int, uint32_t) { count++; });
    return count;
}

std::vector<size_t> Index::GetFieldPositions(int docId, const std::string& term,
                                              char decoration) const {
    std::vector<size_t> out;
    if (docId < 0 || docId >= (int)documents.size()) return out;
    Tuple<std::string, PostingList>* t = dictionary.Find(term);
    if (!t) return out;

    const auto& doc = documents[docId];
    auto dec = [](uint32_t p) -> char {
        uint32_t v = p & 0x7;
        switch (v) {
            case 0: return 'b';
            case 1: return '@';
            case 2: return '#';
            case 3: return '$';
            case 4: return '%';
            default: return 'x';
        }
    };
    auto del = [](uint32_t p) -> uint32_t { return p >> 3; };
    walk_field_posts(t->value, doc.start_position, doc.end_position, decoration,
                     dec, del,
                     [&](int abs_pos, uint32_t) { out.push_back((size_t)abs_pos); });
    return out;
}

void Index::Finalize(){
    dictionary.Optimize(1.5);
}

// ---- Blob serialization ---------------------------------------------------
// Custom format per rank. VM handles paging, so we go for simple readable
// layout rather than mmap-optimized structure.
namespace {
constexpr uint64_t BLOB_MAGIC   = 0x494E444558424C42ULL;  // "INDEXBLB"
constexpr uint64_t BLOB_VERSION = 3;

template <typename T>
void write_pod(std::ostream& o, const T& v) {
    o.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
bool read_pod(std::istream& i, T& v) {
    return static_cast<bool>(i.read(reinterpret_cast<char*>(&v), sizeof(T)));
}

void write_str(std::ostream& o, const std::string& s) {
    uint64_t n = s.size();
    write_pod(o, n);
    if (n) o.write(s.data(), n);
}

bool read_str(std::istream& i, std::string& s) {
    uint64_t n;
    if (!read_pod(i, n)) return false;
    s.resize(n);
    if (n) i.read(s.data(), n);
    return static_cast<bool>(i);
}

void write_str_vec(std::ostream& o, const std::vector<std::string>& v) {
    uint64_t n = v.size();
    write_pod(o, n);
    for (const auto& s : v) write_str(o, s);
}

bool read_str_vec(std::istream& i, std::vector<std::string>& v) {
    uint64_t n;
    if (!read_pod(i, n)) return false;
    v.resize(n);
    for (uint64_t k = 0; k < n; k++) if (!read_str(i, v[k])) return false;
    return true;
}

template <typename T>
void write_pod_vec(std::ostream& o, const std::vector<T>& v) {
    uint64_t n = v.size();
    write_pod(o, n);
    if (n) o.write(reinterpret_cast<const char*>(v.data()), n * sizeof(T));
}

template <typename T>
bool read_pod_vec(std::istream& i, std::vector<T>& v) {
    uint64_t n;
    if (!read_pod(i, n)) return false;
    v.resize(n);
    if (n) i.read(reinterpret_cast<char*>(v.data()), n * sizeof(T));
    return static_cast<bool>(i);
}
}

bool Index::WriteBlob(const std::string& path) const {
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) return false;

    write_pod(o, BLOB_MAGIC);
    write_pod(o, BLOB_VERSION);
    write_pod(o, static_cast<uint64_t>(globalPositionCounter));

    // documents
    write_pod(o, static_cast<uint64_t>(documents.size()));
    for (const auto& d : documents) {
        write_pod(o, static_cast<int32_t>(d.doc_id));
        write_str(o, d.url);
        write_str_vec(o, d.title_words);
        write_pod(o, static_cast<int32_t>(d.hop_distance));
        write_pod(o, static_cast<int32_t>(d.body_length));
        write_pod(o, static_cast<int32_t>(d.start_position));
        write_pod(o, static_cast<int32_t>(d.end_position));
        write_pod(o, d.page_file_rank);
        write_pod(o, d.page_file_num);
        write_pod(o, d.page_file_index);
    }

    // dictionary — count first (iterate once to count, again to write)
    uint64_t dict_count = 0;
    for (auto it = const_cast<HashTable<std::string, PostingList>&>(dictionary).begin();
         it != const_cast<HashTable<std::string, PostingList>&>(dictionary).end(); ++it) {
        ++dict_count;
    }
    write_pod(o, dict_count);

    for (auto it = const_cast<HashTable<std::string, PostingList>&>(dictionary).begin();
         it != const_cast<HashTable<std::string, PostingList>&>(dictionary).end(); ++it) {
        write_str(o, it->key);
        const PostingList& pl = it->value;
        write_pod_vec(o, pl.posts);
        write_pod_vec(o, pl.seek_absolutes);
        write_pod_vec(o, pl.seek_indices);
        write_pod(o, static_cast<int32_t>(pl.last_abs_pos));
        write_pod(o, static_cast<int32_t>(pl.num_docs));
        write_pod(o, static_cast<int32_t>(pl.word_occurrences));
        write_pod(o, static_cast<int32_t>(pl.last_doc_id));
    }

    return static_cast<bool>(o);
}

bool Index::LoadBlob(const std::string& path) {
    std::ifstream i(path, std::ios::binary);
    if (!i) return false;

    uint64_t magic, version, gpc;
    if (!read_pod(i, magic) || magic != BLOB_MAGIC) return false;
    if (!read_pod(i, version) || version != BLOB_VERSION) return false;
    if (!read_pod(i, gpc)) return false;
    globalPositionCounter = static_cast<int>(gpc);

    uint64_t doc_count;
    if (!read_pod(i, doc_count)) return false;
    documents.clear();
    documents.reserve(doc_count);
    for (uint64_t k = 0; k < doc_count; k++) {
        DocumentMetadata d;
        int32_t v;
        if (!read_pod(i, v)) return false; d.doc_id = v;
        if (!read_str(i, d.url)) return false;
        if (!read_str_vec(i, d.title_words)) return false;
        if (!read_pod(i, v)) return false; d.hop_distance = v;
        if (!read_pod(i, v)) return false; d.body_length = v;
        if (!read_pod(i, v)) return false; d.start_position = v;
        if (!read_pod(i, v)) return false; d.end_position = v;
        if (!read_pod(i, d.page_file_rank)) return false;
        if (!read_pod(i, d.page_file_num)) return false;
        if (!read_pod(i, d.page_file_index)) return false;
        documents.push_back(std::move(d));
    }

    uint64_t dict_count;
    if (!read_pod(i, dict_count)) return false;
    for (uint64_t k = 0; k < dict_count; k++) {
        std::string term;
        if (!read_str(i, term)) return false;
        PostingList pl;
        int32_t v;
        if (!read_pod_vec(i, pl.posts)) return false;
        if (!read_pod_vec(i, pl.seek_absolutes)) return false;
        if (!read_pod_vec(i, pl.seek_indices)) return false;
        if (!read_pod(i, v)) return false; pl.last_abs_pos = v;
        if (!read_pod(i, v)) return false; pl.num_docs = v;
        if (!read_pod(i, v)) return false; pl.word_occurrences = v;
        if (!read_pod(i, v)) return false; pl.last_doc_id = v;

        Tuple<std::string, PostingList>* t = dictionary.Find(term, pl);
        t->value = std::move(pl);
    }
    dictionary.Optimize(1.5);
    return true;
}

Index* BuildIndex(){
    Index* index = new Index();

    int rank = 0;
    while(true){
        int fileNum = 0;
        bool foundFile = false;

        while (true){
            string filename = "crawled_page_rank_" + to_string(rank) + "_num_" + to_string(fileNum);

            if (load_page_file(filename) != 0) {
                break;
            }

            foundFile = true;
            PageData page;

            int page_index;
            while ((page_index = get_next_page(page)) != -1) {
                page.page_file_rank = (u_int64_t)rank;
                page.page_file_num = (u_int64_t)fileNum;
                page.page_file_index = (u_int64_t)page_index;
                index->addDocument(page);
            }

            close_page_file();
            fileNum++;
        }
        if (!foundFile && rank > 0) {
            break;
        }
        rank++;

    }
    index->Finalize();
    return index;
}

