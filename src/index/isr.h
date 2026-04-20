// isr.h
#pragma once

#include "index.h"

// index stream reader: steps through one term's posting list, reporting
// absolute position + decoration as it goes. backed by a read-only
// PostingListView into the mmap'd index blob
class ISR {
 private:
  const Index* index;
  Index::PostingListView postingList; // held by value; cheap to copy
  int currentPostIdx; // current index into postingList.posts
  int currentAbsolutePos; // current absolute position in the index

 public:
  ISR(const Index* idx, const std::string& term);

  bool Next();
  bool Seek(int location);
  int GetCurrentPos() const;
  char GetCurrentDecoration() const;
  bool IsValid() const;

  // for constraint solver use
  int GetCurrentDocId() const;
  bool SkipToDoc(int targetDocId);
};
