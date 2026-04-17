// isr.cpp
#include "isr.h"

// ISR constructor - Initializes an iterator that starts at the first post in the posting list and figures out which document that post belongs to.
ISR::ISR(Index* idx, const std::string& term): index(idx), currentPostIdx(0), currentAbsolutePos(0) {
    postingList = idx->getPostingList(term);

    if(IsValid()){
        currentAbsolutePos = index->decodeDelta(postingList->posts[0]);
    }
}

// This function moves the ISR to the next post in the posting list. 
// It updates the current post indx, increments the position counter, 
// and finds out which document the new post belongs to. 
// It returns true if it successfully moved to the next post, or false if there are no more posts.
/*bool ISR::Next() {
    // Check if there's a next post to move to
    if (!IsValid()) {
        return false; // No more posts
    }
    // If we're at the last post, we can't advance
    if (currentPostIdx + 1 >= (int)postingList->posts.size()) {
        // Mark as invalid by advancing past the end
        currentPostIdx = postingList->posts.size();  // Set to size (invalid)
        return false;
    } 

    currentPostIdx++;
    currentAbsolutePos += index->decodeDelta(postingList->posts[currentPostIdx]);

    return true;
}*/
bool ISR::Next() {
    if (!IsValid()) return false;
    
    do {
        if (currentPostIdx + 1 >= (int)postingList->posts.size()) {
            currentPostIdx = postingList->posts.size();
            return false;
        }
        
        currentPostIdx++;
        currentAbsolutePos += index->decodeDelta(postingList->posts[currentPostIdx]);
        
    } while (index->decodeDecoration(postingList->posts[currentPostIdx]) == 4); // Skip EODs
    
    return true;
}

// We need Seek because in the constraint solver, when we find that one ISR is behind the others, we want to be able to jump it forward to catch up instead of having to call Next repeatedly, which would be inefficient. 
// Seek allows us to jump directly to a specific position in the posting list, which can significantly speed up the process of synchronizing multiple ISRs during an AND query.
bool ISR::Seek(int location){
    if(!IsValid()) return false;

    int checkpoint_abs = 0;
    int checkpoint_inx = 0;

    postingList->findCheckpoint(location, checkpoint_abs, checkpoint_inx);

    // Just added this line but I'm not sure if I'll need it - Y
    // Missing Step: jump to checkpoint -
    // We need this because if we don't jump to the checkpoint, we'll be starting from the beginning of the posting list every time we call Seek, which would be inefficient for large posting lists. 
    // By jumping to the checkpoint, we can skip a large portion of the posting list and start our search closer to the target location, which can significantly reduce the number of posts we need to iterate through.
    currentPostIdx = checkpoint_inx;
    currentAbsolutePos = checkpoint_abs;


    while (IsValid() && currentAbsolutePos < location) {
        if (currentPostIdx + 1 >= (int)postingList->posts.size()) return false;
        currentPostIdx++;
        currentAbsolutePos += index->decodeDelta(postingList->posts[currentPostIdx]);
    }

    return currentAbsolutePos >= location;
}

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

// New addition: Skip to the first position in a specific document
// This is used in the constraint solver to advance ISRs that are behind to catch up to the current document being evaluated.
bool ISR::SkipToDoc(int targetDocId) {
    if (!IsValid()) return false;
    
    // Get the document's start position
    Index::DocumentMetadata meta = index->GetDocumentMetadata(targetDocId);
    int targetPos = meta.start_position;
    
    // Use existing Seek to jump to that position
    return Seek(targetPos);
}

bool ISR::IsValid() const {
    return postingList !=nullptr && currentPostIdx < (int)postingList->posts.size();
}

int ISR::GetCurrentPos() const{
    return currentAbsolutePos;
}

char ISR::GetCurrentDecoration() const{
    if (!IsValid()) return -1;
    return index->decodeDecoration(postingList->posts[currentPostIdx]);
}


