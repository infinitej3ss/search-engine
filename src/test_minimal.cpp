//to run
//g++ -std=c++17 test_minimal.cpp index.cpp Common.cpp page_data.cpp -o test_minimal
//./test_minimal
#include "index.h"
#include "page_data.h"
#include <iostream>
#include <vector>
#include <string>

using namespace std;

void PrintPostingList(Index* index, const string& word) {
    cout << "\n=== Posting List for '" << word << "' ===" << endl;
    
    auto pl = index->getPostingList(word);
    if (!pl) {
        cout << "  NOT FOUND!" << endl;
        return;
    }
    
    cout << "  num_docs: " << pl->num_docs << endl;
    cout << "  word_occurrences: " << pl->word_occurrences << endl;
    cout << "  posts.size(): " << pl->posts.size() << endl;
    
    int currentPos = 0;
    for (size_t i = 0; i < pl->posts.size(); i++) {
        uint32_t encoded = pl->posts[i];
        int delta = index->decodeDelta(encoded);
        char decoration = index->decodeDecoration(encoded);
        currentPos += delta;
        
        string dec_str;
        switch(decoration) {
            case 'b': dec_str = "body"; break;
            case '@': dec_str = "title"; break;
            case '#': dec_str = "url"; break;
            case '$': dec_str = "anchor"; break;
            case '%': dec_str = "EOD"; break;
            default: dec_str = "unknown";
        }
        
        cout << "  Post " << i << ": delta=" << delta 
             << ", abs_pos=" << currentPos
             << ", dec=" << dec_str;
        
        if (decoration == '%') {
            cout << " <<< EOD MARKER";
        }
        cout << endl;
    }
}

void PrintDocumentMetadata(Index* index) {
    cout << "\n=== Document Metadata ===" << endl;
    for (int i = 0; i < index->GetDocumentCount(); i++) {
        auto meta = index->GetDocumentMetadata(i);
        cout << "Doc " << i << ": [" << meta.start_position 
             << ", " << meta.end_position << "] - " << meta.url 
             << " (unique words: " << meta.word_count << ")" << endl;
    }
}

int main() {
    cout << "=== Minimal Index Test ===" << endl;
    
    Index* index = new Index();
    
    // Create test documents
    cout << "\n--- Adding Document 1 ---" << endl;
    PageData page1;
    page1.url = "https://example.com/doc1";
    page1.words = {"the", "cat", "sat", "on", "the", "mat"};
    page1.titlewords = {"My", "Cat"};
    page1.anchor_text = {};
    
    index->addDocument(page1);
    
    cout << "\n--- Adding Document 2 ---" << endl;
    PageData page2;
    page2.url = "https://example.com/doc2";
    page2.words = {"the", "dog", "ran", "away"};
    page2.titlewords = {"A", "Dog"};
    page2.anchor_text = {};
    
    index->addDocument(page2);
    
    cout << "\n--- Adding Document 3 ---" << endl;
    PageData page3;
    page3.url = "https://example.com/doc3";
    page3.words = {"the", "bird", "flew"};
    page3.titlewords = {"Flying", "Bird"};
    page3.anchor_text = {};

    index->addDocument(page3);
     index ->Finalize();
    // Print document boundaries
    PrintDocumentMetadata(index);
    
    // Print posting lists
    PrintPostingList(index, "the");
    PrintPostingList(index, "cat");
    PrintPostingList(index, "dog");
    PrintPostingList(index, "bird");
    //Url data
    PrintPostingList(index, "example");
    // PrintPostingList(index, "com");
    // PrintPostingList(index, "doc1");
    // PrintPostingList(index, "doc2");
    // PrintPostingList(index, "doc3");
    
    // Also test a word that appears in only one document
    PrintPostingList(index, "mat");
    
   
    
    delete index;
    return 0;
}