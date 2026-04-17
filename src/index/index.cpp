#include "index.h"
#include "Common.h"
#include "page_data.h"
#include <iostream>
#include <fstream>
#include <set>
#include <cstdint>

using namespace std;

//constructor
Index::Index() : dictionary(CompareEqual, str_hash, 1024), globalPositionCounter(0) {}

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

    if (decoration != '%') {
        pl->word_occurrences++;
    }

    globalPositionCounter++;
}


void Index::addDocument(const PageData& page){
    int docId = (int)documents.size();
    std::set<std::string> uniqueWords;  // Track unique words for word_count
    
    DocumentMetadata doc_data;
    doc_data.doc_id = docId;
    doc_data.url = page.url.c_str();
    doc_data.title_words = page.titlewords;
    doc_data.body_words = page.words;
    doc_data.anchor_texts = page.anchor_text;
    doc_data.hop_distance = static_cast<int>(page.distance_from_seedlist);
    doc_data.start_position = globalPositionCounter;
    
    // Add URL
    // uniqueWords.insert(page.url);
    // addPost(page.url.c_str(), '#', docId);

    std::vector<std::string> urlParts = splitURL(page.url);
    for(const std::string& part : urlParts) {
        uniqueWords.insert(part);
        addPost(part, '#', docId);
    }

    for(const string& word : page.titlewords){
        uniqueWords.insert(word);
        addPost(word, '@', docId);
    }

    for(const string& word : page.words){
        uniqueWords.insert(word);
        addPost(word, 'b', docId);
    }

    for(const string& word : page.anchor_text){
        uniqueWords.insert(word);
        addPost(word, '$', docId);
    }
    
    doc_data.word_count = uniqueWords.size();
    doc_data.end_position = globalPositionCounter;

    // Emit an EOD post for every term that appeared in this document. All EODs
    // share the "one past the last word" slot, so we do not bump the counter.
    for (const std::string& term : uniqueWords) {
        PostingList* pl = getPostingList(term);
        int delta = globalPositionCounter - pl->last_abs_pos;
        pl->addPost(encodePost('%', delta));
        pl->last_abs_pos = globalPositionCounter;
    }

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

void Index::Finalize(){
    dictionary.Optimize(1.5);
}

// ---- Blob serialization ---------------------------------------------------
// Custom format per rank. VM handles paging, so we go for simple readable
// layout rather than mmap-optimized structure.
namespace {
constexpr uint64_t BLOB_MAGIC   = 0x494E444558424C42ULL;  // "INDEXBLB"
constexpr uint64_t BLOB_VERSION = 1;

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
        write_str(o, d.title);
        write_str_vec(o, d.title_words);
        write_str_vec(o, d.body_words);
        write_str_vec(o, d.anchor_texts);
        write_pod(o, static_cast<int32_t>(d.hop_distance));
        write_pod(o, static_cast<int32_t>(d.word_count));
        write_pod(o, static_cast<int32_t>(d.start_position));
        write_pod(o, static_cast<int32_t>(d.end_position));
        write_pod(o, static_cast<int32_t>(d.eod_post_index));
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
        if (!read_str(i, d.title)) return false;
        if (!read_str_vec(i, d.title_words)) return false;
        if (!read_str_vec(i, d.body_words)) return false;
        if (!read_str_vec(i, d.anchor_texts)) return false;
        if (!read_pod(i, v)) return false; d.hop_distance = v;
        if (!read_pod(i, v)) return false; d.word_count = v;
        if (!read_pod(i, v)) return false; d.start_position = v;
        if (!read_pod(i, v)) return false; d.end_position = v;
        if (!read_pod(i, v)) return false; d.eod_post_index = v;
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

            while (get_next_page(page) != -1){
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

