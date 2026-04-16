// test_ranker.cpp
#include "index/simple_api.h"
#include <iostream>

using namespace std;

int main() {
    cout << "=== Testing Index API ===" << endl;
    
    // Create index
    IndexHandle idx = create_index();
    
    // Build test index
    cout << "\nBuilding test index..." << endl;
    
    // Use add_document directly (since build_index_from_crawler may need crawler files)
    const char* title1[] = {"cats", "feline"};
    const char* body1[] = {"cats", "are", "cute", "animals"};
    add_document(idx, "https://example.com/cats", title1, 2, body1, 4, 0);
    
    const char* title2[] = {"dogs", "canine"};
    const char* body2[] = {"dogs", "are", "friendly", "pets"};
    add_document(idx, "https://example.com/dogs", title2, 2, body2, 4, 1);
    
    const char* title3[] = {"pets", "animals"};
    const char* body3[] = {"cats", "dogs", "are", "great", "pets"};
    add_document(idx, "https://example.com/pets", title3, 2, body3, 5, 2);
    
    finalize_index(idx);
    
    // Show document count
    cout << "\nTotal documents: " << get_document_count(idx) << endl;
    
    // Test single word queries
    cout << "\n=== Single Word Queries ===" << endl;
    const char* words[] = {"cats", "dogs", "pets", "the"};
    for (const char* word : words) {
        int freq = get_document_frequency(idx, word);
        cout << "  '" << word << "' appears in " << freq << " documents" << endl;
    }
    
    // Test AND queries
    cout << "\n=== AND Queries ===" << endl;
    
    const char* query1[] = {"cats", "dogs"};
    int result_count;
    int* results = find_and_query(idx, query1, 2, &result_count);
    
    cout << "Query: 'cats' AND 'dogs'" << endl;
    cout << "  Found " << result_count << " documents" << endl;
    for (int i = 0; i < result_count; i++) {
        cout << "    Doc " << results[i] << ": " << get_document_url(idx, results[i]) << endl;
    }
    free_results(results);
    
    // Test another query
    const char* query2[] = {"cats", "pets"};
    results = find_and_query(idx, query2, 2, &result_count);
    
    cout << "\nQuery: 'cats' AND 'pets'" << endl;
    cout << "  Found " << result_count << " documents" << endl;
    for (int i = 0; i < result_count; i++) {
        cout << "    Doc " << results[i] << ": " << get_document_url(idx, results[i]) << endl;
    }
    free_results(results);
    
    // Clean up
    destroy_index(idx);
    
    cout << "\n=== API Test Complete ===" << endl;
    return 0;
}
