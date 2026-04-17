// isr.cpp
#include "isr.h"

// ISR constructor - Initializes an iterator that starts at the first post in the posting list and figures out which document that post belongs to.
ISR::ISR(Index* idx, const std::string& term): index(idx), currentPostIdx(0), currentAbsolutePos(0) {
    postingList = idx->getPostingList(term);

    if(IsValid()){
        currentAbsolutePos = index->decodeDelta(postingList->posts[0]);
    }
}

// Advances the ISR to the next WORD post (skipping EOD markers).
//
// How it works:
//   1. Checks if current position is valid
//   2. Loops until it finds a non-EOD post:
//      - Moves to next post index
//      - Accumulates delta to reconstruct absolute position
//   3. Stops when a word post (not '%') is found or end of list is reached
//
// Returns:
//   true  - successfully moved to next word post
//   false - reached end of posting list (no more posts)
//
// Note: EOD markers ('%') are skipped because they were used as document boundaries,
//       not actual word occurrences that should be returned to the ranker.

bool ISR::Next() {
    if (!IsValid()) return false;

    do {
        if (currentPostIdx + 1 >= (int)postingList->posts.size()) {
            currentPostIdx = postingList->posts.size();
            return false;
        }

        currentPostIdx++;
        currentAbsolutePos += index->decodeDelta(postingList->posts[currentPostIdx]);

    } while (index->decodeDecoration(postingList->posts[currentPostIdx]) == '%');

    return true;
}


// Jumps to the first post whose absolute position is >= the target location.
//
// How it works:
//   1. Uses checkpoint system to jump near the target (optimization)
//   2. If no checkpoint exists, starts from the beginning
//   3. Linearly advances through posts until reaching or passing target
//   4. Skips past any EOD markers that would land on document boundaries
//
// Parameters:
//   location - target absolute position to seek to
//
// Returns:
//   true  - successfully positioned at or after target location
//   false - target location is beyond the end of the posting list
//
// Why this matters: Used by the constraint solver to efficiently synchronize
// multiple ISRs during AND queries without calling Next() repeatedly.

// We need Seek because in the constraint solver, when we find that one ISR is behind the others, we want to be able to jump it forward to catch up instead of having to call Next repeatedly, which would be inefficient. 
// Seek allows us to jump directly to a specific position in the posting list, which can significantly speed up the process of synchronizing multiple ISRs during an AND query.
bool ISR::Seek(int location){
    if(!IsValid()) return false;

    int checkpoint_abs = 0;
    int checkpoint_inx = 0;
    bool has_checkpoint = postingList->findCheckpoint(location, checkpoint_abs, checkpoint_inx);

    if (has_checkpoint) {
        currentPostIdx = checkpoint_inx;
        currentAbsolutePos = checkpoint_abs;
    } else {
        // No checkpoint: start at post 0 with its delta (matches constructor init).
        currentPostIdx = 0;
        currentAbsolutePos = index->decodeDelta(postingList->posts[0]);
    }

    while (IsValid() && currentAbsolutePos < location) {
        if (currentPostIdx + 1 >= (int)postingList->posts.size()) {
            currentPostIdx = postingList->posts.size();
            return false;
        }
        currentPostIdx++;
        currentAbsolutePos += index->decodeDelta(postingList->posts[currentPostIdx]);
    }

    // Skip past any EOD posts — doc boundaries are shared slots, so Seek can
    // land on a previous doc's EOD when target == shared boundary position.
    while (IsValid() && index->decodeDecoration(postingList->posts[currentPostIdx]) == '%') {
        if (currentPostIdx + 1 >= (int)postingList->posts.size()) {
            currentPostIdx = postingList->posts.size();
            return false;
        }
        currentPostIdx++;
        currentAbsolutePos += index->decodeDelta(postingList->posts[currentPostIdx]);
    }

    return IsValid() && currentAbsolutePos >= location;
}


// Determines which document contains the current absolute position.
//
// How it works:
//   Performs binary search through the document metadata array using the
//   current absolute position. Each document has start_position and end_position
//   boundaries. The position belongs to the document where:
//     start_position <= position <= end_position
//
// Returns:
//   Document ID (0-based index) if found, -1 if invalid
//
// Why we need this: Since we don't store docId in individual posts to save space,
// we must reconstruct the document ID from the absolute position using the
// document boundaries stored separately.

// New addition: Get current document ID by binary searching EOD boundaries
// Without this method, the solver has no way to know which document a position belongs to
// In the constraint solver, we need to compare document IDs across ISRs to determine if they are all on the same document (for AND queries) or to advance ISRs that are behind.
int ISR::GetCurrentDocId() const {
    if (!IsValid()) return -1;
    
    // Binary search through documents to find which document 
    // contains the current absolute position
    int left = 0;
    int right = index->GetDocumentCount() - 1;
    
    while (left <= right) {
        int mid = (left + right) / 2;
        Index::DocumentMetadata meta = index->GetDocumentMetadata(mid);
        
        if (currentAbsolutePos < meta.start_position) {
            right = mid - 1;
        } else if (currentAbsolutePos > meta.end_position) {// changed >= to >
            left = mid + 1;
        } else {
            return meta.doc_id;  // Found the document!
        }
    }
    return -1;
}


// Jumps to the first position within a specific target document.
//
// How it works:
//   1. Retrieves the document's start_position from metadata
//   2. Calls Seek() to jump to that position
//
// Parameters:
//   targetDocId - the document ID to seek to
//
// Returns:
//   true  - successfully positioned at the start of target document
//   false - target document doesn't exist or cannot be reached
//
// Why this matters: Used by the constraint solver to advance ISRs that are
// behind during AND query processing, allowing them to catch up efficiently.

// New addition: Skip to the first position in a specific document
// This is used in the constraint solver to advance ISRs that are behind to catch up to the current document being evaluated.
bool ISR::SkipToDoc(int targetDocId) {
    if (!IsValid()) return false;
    if (targetDocId < 0 || targetDocId >= index->GetDocumentCount()) return false;

    Index::DocumentMetadata meta = index->GetDocumentMetadata(targetDocId);
    return Seek(meta.start_position);
}


// Checks whether the ISR currently points to a valid post.
// Returns:
//   true  - currentPostIdx is within the posting list bounds
//   false - ISR is at end of list or was never initialized

bool ISR::IsValid() const {
    return postingList !=nullptr && currentPostIdx < (int)postingList->posts.size();
}


// Returns the reconstructed absolute position of the current post.
// This value is built by accumulating deltas from previous posts.

int ISR::GetCurrentPos() const{
    return currentAbsolutePos;
}


// Returns the decoration type of the current post.
// Possible return values:
//   '@' - title word (highest importance)
//   'b' - body word (normal importance)
//   '#' - URL part (medium importance)
//   '$' - anchor text (high authority)
//   '%' - EOD marker (metadata, not a word)
//
// Returns -1 if the ISR is invalid.

char ISR::GetCurrentDecoration() const{
    if (!IsValid()) return -1;
    return index->decodeDecoration(postingList->posts[currentPostIdx]);
}


