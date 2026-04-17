// test_index.cpp
#include "index.h"
#include "isr.h"
#include "constraint_solver.h"
#include <iostream>
#include <chrono>

using namespace std;

// Helper function to print section headers
void printSection(const string& title) {
    cout << "\n========================================" << endl;
    cout << title << endl;
    cout << "========================================" << endl;
}

int main() {
    Index* index = new Index();
    // ===== CREATE TEST DATA =====

    // Create a fake page manually: testPage (since we may not have crawler files)
    PageData testPage;
    testPage.url = "https://example.com/test";
    testPage.titlewords = {"hello", "world"};
    testPage.words = {"this", "is", "the", "test"};
    testPage.anchor_text = {"link"};
    testPage.distance_from_seedlist = 0;
    index->addDocument(testPage);

    // Document 1: Cat Page
    PageData catPage;
    catPage.url = "https://example.com/cat";
    catPage.titlewords = {"cat", "feline", "pets"};
    catPage.words = {"cats", "are", "cute", "cats", "like", "milk"};
    catPage.anchor_text = {"cat", "kitten"};
    catPage.distance_from_seedlist = 0;
    index->addDocument(catPage);
    cout << "Added CatPage" << endl;

    // Document 2: Dog Page
    PageData dogPage;
    dogPage.url = "https://example.com/dog";
    dogPage.titlewords = {"dogs", "canine", "pets"};
    dogPage.words = {"dogs", "are", "loyal", "dogs", "like", "walks"};
    dogPage.anchor_text = {"dog", "puppy"};
    dogPage.distance_from_seedlist = 1;
    index->addDocument(dogPage);
    cout << "Added DogPage" << endl;

    // Document 3: Both cats and dogs
    PageData petsPage;
    petsPage.url = "https://example.com/pets";
    petsPage.titlewords = {"pets", "animals"};
    petsPage.words = {"cats", "and", "dogs", "are", "great", "pets"};
    petsPage.anchor_text = {"pets"};
    petsPage.distance_from_seedlist = 2;
    index->addDocument(petsPage);
    cout << "Added PetsPage" << endl;

    // Document 4: Word with mutliple positions (for phrase testing)
    PageData phrasePage;
    phrasePage.url = "https://example.com/repeat";
    phrasePage.titlewords = {"hello"};
    phrasePage.words = {"cat", "sat", "cat", "sat", "cat"};
    phrasePage.anchor_text = {};
    phrasePage.distance_from_seedlist = 1;
    index->addDocument(phrasePage);
    cout << "Added PhrasePage" << endl;

    cout << "\nTotal documents indexed: " << index->GetDocumentCount() << endl;

    // ===== TEST 0: Check if we can retrieve posting lists =====
    string word = "hello";
    Index::PostingList* pl = index->getPostingList(word);
    if (pl && pl->posts.size() > 0) {
        cout << "Found '" << word << "' in index" << endl;
    } else {
        cout << "This word '" << word << "' was not found" << endl;
    }
    
    // Check document metadata
    cout << "Document count: " << index->GetDocumentCount() << endl;
    auto meta = index->GetDocumentMetadata(0);
    cout << "URL: " << meta.url << endl;
    
    // ===== TEST 1: Multiple Words in a Document =====
    cout << "\n--- Testing Multiple Words ---" << endl;
    
    Index::PostingList* pl2 = index->getPostingList(string("world"));
    if (pl2 && pl2->posts.size() > 0) {
        cout << "'world' found in index" << endl;
        cout << "   Total posts (including EOD): " << pl2->posts.size() << endl;
        cout << "   Word occurrences (excluding EOD): " << pl2->word_occurrences << endl;
    } else {
            cout << "'world' not found" << endl;
    }
    
    Index::PostingList* pl3 = index->getPostingList(string("test"));
    if (pl3 && pl3->posts.size() > 0) {
        cout << "'test' found in index" << endl;
        cout << "   Total posts (including EOD): " << pl3->posts.size() << endl;
        cout << "   Word occurrences (excluding EOD): " << pl3->word_occurrences << endl;
    } else {
        cout << "'test' not found" << endl;
    }

    // ===== NEW TEST 2: Multiple Documents =====
    cout << "\n--- Testing Multiple Documents ---" << endl;
    
    PageData testPage2;
    testPage2.url = "https://example.com/test2";
    testPage2.titlewords = {"hello", "again"};
    testPage2.words = {"goodbye"};
    testPage2.anchor_text = {};
    testPage2.distance_from_seedlist = 1;
    
    index->addDocument(testPage2);
    
    cout << "Document count: " << index->GetDocumentCount() << endl;
    
    int freq = index->GetDocumentFrequency(string("hello"));
    cout << "'hello' appears in " << freq << " documents" << endl;
    
    // Check metadata for second document
    auto meta2 = index->GetDocumentMetadata(1);
    cout << "Document 1 URL: " << meta2.url << endl;

    // ===== NEW TEST 3: Position Reconstruction =====
    cout << "\n--- Testing Position Reconstruction ---" << endl;

    Index::PostingList* pl_hello = index->getPostingList(string("hello"));
    if (pl_hello) {
        cout << "Total posts for 'hello': " << pl_hello->posts.size() << endl;
        int absPos = 0;
        for (size_t i = 0; i < pl_hello->posts.size(); i++) {
            uint32_t rawPost = pl_hello->posts[i];
            int delta = index->decodeDelta(rawPost);
            char dec = index->decodeDecoration(rawPost);
            absPos += delta;
            
            // Decode decoration character to string
            string dec_str;
            switch(dec) {
                case 'b': dec_str = "body"; break;
                case '@': dec_str = "title"; break;
                case '#': dec_str = "url"; break;
                case '$': dec_str = "anchor"; break;
                case '%': dec_str = "EOD"; break;
                default: dec_str = "unknown";
            }
            
            cout << "  Post " << i << ": raw=" << rawPost 
                 << ", delta=" << delta 
                 << ", absPos=" << absPos 
                 << ", dec='" << dec << "' (" << dec_str << ")" << endl;
        }
    } else {
        cout << "No posting list found for 'hello'" << endl;
    }

    // Also test 'world' to see its posts
    cout << "\n--- Posts for 'world' ---" << endl;
    Index::PostingList* pl_world = index->getPostingList(string("world"));
    if (pl_world) {
        cout << "Total posts for 'world': " << pl_world->posts.size() << endl;
        int absPos = 0;
        for (size_t i = 0; i < pl_world->posts.size(); i++) {
            uint32_t rawPost = pl_world->posts[i];
            int delta = index->decodeDelta(rawPost);
            char dec = index->decodeDecoration(rawPost);
            absPos += delta;
            
            string dec_str;
            switch(dec) {
                case 'b': dec_str = "body"; break;
                case '@': dec_str = "title"; break;
                case '#': dec_str = "url"; break;
                case '$': dec_str = "anchor"; break;
                case '%': dec_str = "EOD"; break;
                default: dec_str = "unknown";
            }
            
            cout << "  Post " << i << ": delta=" << delta 
                 << ", absPos=" << absPos 
                 << ", dec='" << dec << "' (" << dec_str << ")" << endl;
        }
    }

    // ========= More tests ========== //
    // ===== TEST 1: Basic Term Lookup =====
    printSection("Test 1: Basic Term Lookup");
    
    vector<string> testTerms = {"cats", "dogs", "pets", "feline", "canine", "nonexistent"};
    for (const string& term : testTerms) {
        auto pl_term = index->getPostingList(term);
        if (pl_term && pl_term->word_occurrences > 0) {
            cout << "Yes, '" << term << "' found in " 
                 << pl_term->num_docs << " documents, "
                 << pl_term->word_occurrences << " total occurrences" << endl;
        } else {
            cout << "No, '" << term << "' not found" << endl;
        }
    }

    // ===== TEST 2: Document Frequency =====
    printSection("Test 2: Document Frequency (DF)");
    
    cout << "Document Frequency (how many docs contain each term):" << endl;
    for (const string& term : {"cats", "dogs", "pets", "animals"}) {
        int df = index->GetDocumentFrequency(term);
        cout << "  '" << term << "' appears in " << df << " document(s)" << endl;
    }
    
    delete index;
    return 0;
}