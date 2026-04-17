// isr.h
#pragma once

#include "index.h"  // Need Index class for DocumentMetadata

class ISR {
private:
    Index* index;                       // Pointer to index (for metadata lookup)
    Index::PostingList* postingList;    // The posting list we're iterating
    int currentPostIdx;                     // Current position in posting list. Which post number (0, 1, 2...)
    int currentAbsolutePos;                   // Cached current document ID

    // Helper: find the document ID for the current post index
    int findCurrentDocId() const;
    
public:
    // Constructor
    ISR(Index* idx, const std::string& term);
    
    // Move to next post
    bool Next();

    bool Seek(int location);
    
    // Get current delta (for position reconstruction)
    int GetCurrentPos() const;
    
    // Get decoration info
    char GetCurrentDecoration() const;
    
    // Check if iterator is valid
    bool IsValid() const;

    // New methods for constraint solver
    int GetCurrentDocId() const;    // Returns document ID of current post
    bool SkipToDoc(int targetDocId);   // Skips to the first post with doc ID >= targetDocId
    
   
};