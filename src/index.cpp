#include "index.h"
#include "Common.h"
#include "page_data.h"
#include <iostream>
#include <set>

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
    
    // Check if this is a new document for this term
    bool isNewDocument = (pl->last_doc_id != docId);
    
    if (isNewDocument) {
        // If we had a previous document, add EOD for it
        if (pl->last_doc_id != -1) {
            int delta = globalPositionCounter - pl->last_abs_pos;
            pl->addPost(encodePost('%', delta));
            pl->last_abs_pos = globalPositionCounter;
            globalPositionCounter++;
        }
        
        // Increment document count for this term
        pl->num_docs++;
        pl->last_doc_id = docId;
    }
    
    // Add the content post
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
    // Add final EOD markers for all posting lists
    for (auto it = dictionary.begin(); it != dictionary.end(); ++it) {
        PostingList& pl = it->value;
        if (pl.last_doc_id != -1) {
            int delta = globalPositionCounter - pl.last_abs_pos;
            pl.addPost(encodePost('%', delta));
            pl.last_abs_pos = globalPositionCounter;
            globalPositionCounter++;
        }
    }
    dictionary.Optimize(1.5);
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

