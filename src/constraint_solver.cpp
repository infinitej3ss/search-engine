// constraint_solver.cpp
#include "constraint_solver.h"
#include <set>

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

    // Classical multi-ISR AND: advance every ISR to the max docId across them.
    // When all land on the same doc, it's a match; advance past and repeat.
    while (true) {
        int maxDoc = -1;
        bool allValid = true;

        for (ISR* isr : isrs) {
            if (!isr->IsValid()) { allValid = false; break; }
            int docId = isr->GetCurrentDocId();
            if (docId < 0) { allValid = false; break; }
            if (docId > maxDoc) maxDoc = docId;
        }
        if (!allValid) break;

        bool allMatch = true;
        for (ISR* isr : isrs) {
            if (isr->GetCurrentDocId() != maxDoc) { allMatch = false; break; }
        }

        if (allMatch) {
            results.push_back(maxDoc);
            for (ISR* isr : isrs) {
                if (!isr->SkipToDoc(maxDoc + 1)) { allValid = false; break; }
            }
            if (!allValid) break;
        } else {
            for (ISR* isr : isrs) {
                if (isr->GetCurrentDocId() < maxDoc) {
                    if (!isr->SkipToDoc(maxDoc)) { allValid = false; break; }
                }
            }
            if (!allValid) break;
        }
    }

    for (ISR* isr : isrs) delete isr;
    return results;
}

std::vector<int> ConstraintSolver::FindOrQuery(const std::vector<std::string>& terms) {
    if (terms.empty()) return {};

    std::set<int> docSet;

    for (const auto& term : terms) {
        ISR isr(index, term);
        while (isr.IsValid()) {
            int docId = isr.GetCurrentDocId();
            if (docId < 0) break;
            docSet.insert(docId);
            if (!isr.SkipToDoc(docId + 1)) break;
        }
    }

    return std::vector<int>(docSet.begin(), docSet.end());
}