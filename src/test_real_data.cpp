#include "index.h"
#include "isr.h"
#include "constraint_solver.h"
#include <iostream>
#include <vector>
#include <string>

using namespace std;

void AnalyzePostingListCreation(Index* index, const string& word, int maxPosts = -1) {
    cout << "\n=== Detailed Analysis of Posting List for '" << word << "' ===" << endl;
    
    auto pl = index->getPostingList(word);
    if (!pl) {
        cout << "No posting list found!" << endl;
        return;
    }
    
    cout << "Total posts: " << pl->posts.size() << endl;
    cout << "Word occurrences: " << pl->word_occurrences << endl;
    cout << "Unique documents (num_docs): " << pl->num_docs << endl;
    cout << "Last doc ID tracked: " << pl->last_doc_id << endl;
    cout << "Last absolute position: " << pl->last_abs_pos << endl;
    
    // Print ALL posts
    cout << "\n=== All Posts ===" << endl;
    int currentPos = 0;
    int lastDocId = -1;
    int postsPrinted = 0;
    
    for (size_t i = 0; i < pl->posts.size(); i++) {
        if (maxPosts > 0 && postsPrinted >= maxPosts) {
            cout << "  ... and " << (pl->posts.size() - i) << " more posts" << endl;
            break;
        }
        
        uint32_t encoded = pl->posts[i];
        int delta = index->decodeDelta(encoded);
        char decoration = index->decodeDecoration(encoded);
        currentPos += delta;
        
        // Find which document this position belongs to
        int docId = -1;
        for (int d = 0; d < index->GetDocumentCount(); d++) {
            auto meta = index->GetDocumentMetadata(d);
            if (currentPos >= meta.start_position && currentPos < meta.end_position) {
                docId = d;
                break;
            }
        }
        
        string dec_str;
        switch(decoration) {
            case 'b': dec_str = "body"; break;
            case '@': dec_str = "title"; break;
            case '#': dec_str = "url"; break;
            case '$': dec_str = "anchor"; break;
            case '%': dec_str = "EOD"; break;
            default: dec_str = "unknown";
        }
        
        // Print every post with clear formatting
        cout << "  Post " << i << ": doc=" << docId 
             << ", abs_pos=" << currentPos
             << ", dec=" << dec_str 
             << " (" << decoration << ")"
             << ", delta=" << delta;
        
        if (docId != lastDocId && lastDocId != -1) {
            cout << " <<< NEW DOCUMENT";
        }
        if (decoration == 4) {
            cout << " <<< EOD MARKER";
        }
        cout << endl;
        
        lastDocId = docId;
        postsPrinted++;
    }
    
    // Also show document boundaries for context
    cout << "\n=== Document Boundaries for Context ===" << endl;
    for (int d = 0; d < min(10, index->GetDocumentCount()); d++) {
        auto meta = index->GetDocumentMetadata(d);
        cout << "  Doc " << d << ": [" << meta.start_position 
             << ", " << meta.end_position << "] - " << meta.url << endl;
    }
    if (index->GetDocumentCount() > 10) {
        cout << "  ... and " << (index->GetDocumentCount() - 10) << " more documents" << endl;
    }
}

// Debug function for document boundaries (not a class method)
void DebugDocumentBoundaries(Index* index) {
    cout << "\n=== Document Boundary Debug ===" << endl;
    for (int i = 0; i < index->GetDocumentCount(); i++) {
        auto meta = index->GetDocumentMetadata(i);
        cout << "Doc " << i << " (" << meta.url << "):" << endl;
        cout << "  start_position: " << meta.start_position << endl;
        cout << "  end_position: " << meta.end_position << endl;
        cout << "  word_count: " << meta.word_count << endl;
    }
}

// Debug function for posting lists
void DebugPostingList(Index* index, const string& word) {
    cout << "\n=== Debug posting list for '" << word << "' ===" << endl;
    auto pl = index->getPostingList(word);
    if (!pl) {
        cout << "Posting list not found!" << endl;
        return;
    }
    
    cout << "num_docs: " << pl->num_docs << endl;
    cout << "word_occurrences: " << pl->word_occurrences << endl;
    cout << "posts.size(): " << pl->posts.size() << endl;
    
    // Decode and display first 20 posts
    int currentPos = 0;
    int lastDocId = -1;
    
    for (size_t i = 0; i < min(pl->posts.size(), (size_t)50); i++) {
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
        
        // Find which document this position belongs to
        int docId = -1;
        for (int d = 0; d < index->GetDocumentCount(); d++) {
            auto meta = index->GetDocumentMetadata(d);
            if (currentPos >= meta.start_position && currentPos < meta.end_position) {
                docId = d;
                break;
            }
        }
        
        if (docId != lastDocId) {
            cout << "  Post " << i << ": abs_pos=" << currentPos
                 << ", doc=" << docId
                 << ", dec=" << dec_str << " (" << decoration << ")"
                 << ", delta=" << delta << endl;
            lastDocId = docId;
        } else {
            cout << "  Post " << i << ": abs_pos=" << currentPos
                 << ", doc=" << docId
                 << ", dec=" << dec_str << " (" << decoration << ")"
                 << ", delta=" << delta << " (same doc)" << endl;
        }
    }
    
    if (pl->posts.size() > 50) {
        cout << "  ... and " << (pl->posts.size() - 50) << " more posts" << endl;
    }
}

// Debug function to trace through an ISR
void DebugISR(Index* index, const string& word) {
    cout << "\n=== Debugging ISR for '" << word << "' ===" << endl;
    ISR isr(index, word);
    
    int postCount = 0;
    int lastDocId = -1;
    
    while (isr.IsValid() && postCount < 200) {
        int docId = isr.GetCurrentDocId();
        int pos = isr.GetCurrentPos();
        char dec = isr.GetCurrentDecoration();
        
        string dec_str;
        switch(dec) {
            case 0: dec_str = "body"; break;
            case 1: dec_str = "title"; break;
            case 2: dec_str = "url"; break;
            case 3: dec_str = "anchor"; break;
            case 4: dec_str = "EOD"; break;
            default: dec_str = "unknown";
        }
        
        if (docId != lastDocId) {
            cout << "  Post " << postCount << ": doc=" << docId 
                 << ", pos=" << pos << ", dec=" << dec_str << endl;
            lastDocId = docId;
        }
        
        if (!isr.Next()) break;
        postCount++;
    }
    
    if (postCount >= 100) {
        cout << "  ... (stopped after 100 posts)" << endl;
    }
}

int main() {
    cout << "=== Building Index from Crawler Data ===" << endl;
    
    Index* index = BuildIndex();
    
    cout << "\n=== Index Statistics ===" << endl;
    cout << "Total documents indexed: " << index->GetDocumentCount() << endl;
     // Debug document boundaries
    DebugDocumentBoundaries(index);
    
    // Debug specific words
    DebugPostingList(index, "the");
    DebugPostingList(index, "on");
    //DebugPostingList(index, "with");
    //DebugPostingList(index, "and");
    
    // Debug ISR behavior
    //DebugISR(index, "the");
    //DebugISR(index, "on");
    
    // Then run your AND queries...
    cout << "\n=== Analyzing Posting Lists ===" << endl;
    AnalyzePostingListCreation(index, "the");
    AnalyzePostingListCreation(index, "on");
    //AnalyzePostingListCreation(index, "with");
    
    //delete index;
    //return 0;
    
//     // Print all document URLs to see what's in the index
//     cout << "\n=== Documents in Index ===" << endl;
//     for (int i = 0; i < index->GetDocumentCount(); i++) {
//         auto meta = index->GetDocumentMetadata(i);
//         cout << "  Doc " << i << ": " << meta.url << endl;
//     }
    
//     // Try common words
//     cout << "\n=== Word Frequency Tests ===" << endl;
//     vector<string> testWords = {"the", "and", "to", "of", "a", "in", "for", "on", "with", "is", "at"};
    
//     for (const string& word : testWords) {
//         int freq = index->GetDocumentFrequency(word);
//         if (freq > 0) {
//             cout << "  '" << word << "' appears in " << freq << " documents" << endl;
//         }
//     }

//     // Replace the AND query section with this:
// cout << "\n=== AND Query Tests with Real Words ===" << endl;
// ConstraintSolver solver(index);

// // Use words with lower frequency first
// vector<vector<string>> queries = {
//     {"the", "on"},      // 18 and 31 documents
//     {"the", "with"},    // 18 and 37 documents
//     {"on", "with"},     // 31 and 37 documents
//     {"the", "a"}        // 18 and 73 documents
// };

// for (const auto& query : queries) {
//     cout << "Query: '" << query[0] << "' AND '" << query[1] << "'" << endl;
    
//     auto start = chrono::high_resolution_clock::now();
//     vector<int> results = solver.FindAndQuery(query);
//     auto end = chrono::high_resolution_clock::now();
//     auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    
//     cout << "  Documents found: " << results.size() << endl;
//     cout << "  Time: " << duration.count() << " ms" << endl;
    
//     // Show first 3 results
//     for (int i = 0; i < min(3, (int)results.size()); i++) {
//         auto meta = index->GetDocumentMetadata(results[i]);
//         cout << "    " << results[i] << ": " << meta.url << endl;
//     }
//     cout << endl;
// }


// // Verify Document Frequency
// cout << "\n=== Debug: Checking 'and' frequency ===" << endl;
// string word = "and";
// auto pl_and = index->getPostingList(word);  // Use string, not const char*
// if (pl_and) {
//     cout << "  num_docs (unique documents): " << pl_and->num_docs << endl;
//     cout << "  word_occurrences (total posts): " << pl_and->word_occurrences << endl;
//     cout << "  posts.size(): " << pl_and->posts.size() << endl;
// }


// // // Test specific unique words
// // cout << "\n=== Testing with URL-specific words ===" << endl;
// // vector<vector<string>> urlQueries = {
// //     {"github", "com"},
// //     {"google", "com"},
// //     {"wikipedia", "org"},
// //     {"youtube", "com"}
// // };

// // for (const auto& query : urlQueries) {
// //     vector<int> results = solver.FindAndQuery(query);
// //     cout << "Query: '" << query[0] << "' AND '" << query[1] << "'" << endl;
// //     cout << "  Documents found: " << results.size() << endl;
// //     for (int i = 0; i < min(3, (int)results.size()); i++) {
// //         auto meta = index->GetDocumentMetadata(results[i]);
// //         cout << "    " << results[i] << ": " << meta.url << endl;
// //     }
// //     cout << endl;
// // }
    
    delete index;
    return 0;
}

// Integrate with ranker - They can use ConstraintSolver::FindAndQuery()