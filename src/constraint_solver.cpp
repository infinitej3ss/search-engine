// constraint_solver.cpp
#include "constraint_solver.h"

ConstraintSolver::ConstraintSolver(Index* idx) : index(idx) {}

// AND query - find documents containing ALL terms
std::vector<int> ConstraintSolver::FindAndQuery(const std::vector<std::string>& terms) {
    // If there are no terms, return empty result
    if (terms.empty()) return {};
    
    // Create ISRs for each term
    // Each ISR knows how to traverse that term's posting list
    // If ANY term doesn't exist in the index, the AND query returns nothing (can't have all the terms if one is missing)
    std::vector<ISR*> isrs;
    for (const auto& term : terms) {
        ISR* isr = new ISR(index, term);
        if (!isr->IsValid()) {
            // Term not found - AND query returns nothing
            for (ISR* i : isrs) delete i;
            delete isr;
            return {};
        }
        isrs.push_back(isr);
    }
    
    std::vector<int> results;
    
    // Continuously finds matching documents until we run out of postings
    while (true) {
        // Find the smallest current document ID
        // This is the document ID that all ISRs need to catch up to in order to find a match
        // i.e. they all need to be in the same doc in order for them to match a query that requires all terms
        int currentDoc = -1;
        bool allValid = true;
        
        for (ISR* isr : isrs) {
            if (!isr->IsValid()) {
                allValid = false;
                break;
            }
            int docId = isr->GetCurrentDocId();
            if (currentDoc == -1 || docId < currentDoc) {
                currentDoc = docId;
            }
        }
        
        if (!allValid) break;
        
        // Check if all ISRs are at the same document
        bool allMatch = true;
        for (ISR* isr : isrs) {
            if (isr->GetCurrentDocId() != currentDoc) {
                allMatch = false;
                break;
            }
        }
        
        // If all ISRs are at the same document, we found a match for the AND query
        if (allMatch) {
            results.push_back(currentDoc);
            // Advance all ISRs past this document
            for (ISR* isr : isrs) {
                // Skip to the next document
                if (!isr->SkipToDoc(currentDoc + 1)) {
                    // Can't reach next document, we're done
                    for (ISR* i : isrs) delete i;
                    return results;
                }
            }
        } else {
            // Advance ISRs that are behind to catch up to the currentDoc
            for (ISR* isr : isrs) {
                if (isr->GetCurrentDocId() < currentDoc) {
                    if (!isr->SkipToDoc(currentDoc)) {
                        // Can't reach currentDoc - no more matches
                        for (ISR* i : isrs) delete i;
                        return results;
                    }
                }
            }
        }
    }
    
    // Clean up - delete all ISRs
    for (ISR* isr : isrs) delete isr;
    return results;
}

std::vector<int> ConstraintSolver::FindOrQuery(const std::vector<std::string>& terms) {
    if (terms.empty()) return {};
    
    // For OR query, we need to merge all unique document IDs
    // This is simpler - just collect all docs from all terms
    std::vector<int> results;
    
    for (const auto& term : terms) {
        ISR isr(index, term);
        while (isr.IsValid()) {
            int docId = isr.GetCurrentDocId();
            // Avoid duplicates
            if (std::find(results.begin(), results.end(), docId) == results.end()) {
                results.push_back(docId);
            }
            // Move to next document (skip all posts in current doc)
            isr.SkipToDoc(docId + 1);
        }
    }
    
    return results;
}