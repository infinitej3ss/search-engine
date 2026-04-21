// isr.h
#pragma once

#include "index.h"
#include <memory>
#include <string>
#include <vector>

// abstract index stream reader. each concrete ISR walks the index
// reporting a sequence of matches; a match has a start / end position
// (equal for a single word, wider for phrase / and / container matches).
// Next() advances past the current match; Seek(loc) jumps forward to the
// first match whose start position is >= loc.
class ISR {
 protected:
  const Index* index;
  explicit ISR(const Index* idx) : index(idx) {}

 public:
  virtual ~ISR() = default;
  ISR(const ISR&) = delete;
  ISR& operator=(const ISR&) = delete;

  // advance past the current match. returns true if a new match exists
  virtual bool Next() = 0;

  // position iterator at the first match with start >= location.
  // returns false if no such match remains
  virtual bool Seek(int location) = 0;

  // start / end absolute positions of the current match. equal for words
  virtual int GetStartLocation() const = 0;
  virtual int GetEndLocation() const = 0;

  virtual bool IsValid() const = 0;

  // decoration of the current match, or 0 if unknown / mixed. only ISRWord
  // reports a real decoration; ISRPhrase uses this to require all children
  // share the same field (body / title / url / anchor)
  virtual char GetCurrentDecoration() const { return 0; }

  // default helpers implemented on top of the virtual interface. use the
  // doc-range table to map the current match to a doc id, and seek to the
  // start of a given doc. composite ISRs inherit these unchanged
  virtual int GetCurrentDocId() const;
  virtual bool SkipToDoc(int targetDocId);
};

// single-term ISR. walks one posting list, skipping EOD ('%') posts so
// document boundaries stay invisible to callers
class ISRWord : public ISR {
 private:
  Index::PostingListView postingList; // held by value; cheap to copy
  int currentPostIdx; // current index into postingList.posts
  int currentAbsolutePos; // current absolute position in the index

 public:
  ISRWord(const Index* idx, const std::string& term);

  bool Next() override;
  bool Seek(int location) override;

  int GetStartLocation() const override { return currentAbsolutePos; }
  int GetEndLocation() const override { return currentAbsolutePos; }
  bool IsValid() const override;

  // word-only: decoration of the post the iterator is parked on.
  // 'b' body, '@' title, '#' url, '$' anchor. never '%' — Next/Seek skip EOD
  char GetCurrentDecoration() const override;
};

// doc-boundary walker. backed by the Index's doc-range table — no
// dedicated '%'-only posting list exists in the blob (index_builder emits
// an EOD into every term's list). a "match" here is one entire document;
// start = doc start, end = doc end. used by ISRAnd / ISRContainer to pin
// matches to a single document
class ISREndDoc : public ISR {
 private:
  int numDocs;
  int currentDoc;
  int currentStart;
  int currentEnd;

 public:
  explicit ISREndDoc(const Index* idx);

  bool Next() override;
  bool Seek(int location) override;

  int GetStartLocation() const override { return currentStart; }
  int GetEndLocation() const override { return currentEnd; }
  bool IsValid() const override { return currentDoc >= 0 && currentDoc < numDocs; }

  // index of the current doc. faster than the base class binary search
  int GetCurrentDocIndex() const { return currentDoc; }
  int GetCurrentDocId() const override { return currentDoc; }

  // position the stream on the doc containing loc, or the next doc if
  // loc falls in a gap. returns false if loc is past the last doc
  bool SkipToDocContaining(int location);
};

// conjunction: emits one match per doc that contains every child. match
// span is the widest of the children's first occurrences. used
// as the coordination spine for AND retrieval
class ISRAnd : public ISR {
 private:
  std::vector<std::unique_ptr<ISR>> children;
  ISREndDoc docs;
  int currentStart;
  int currentEnd;
  int currentDoc;
  bool valid;

  // align every child to doc >= minDoc and emit the first doc where all
  // children coincide. returns false if any child exhausts
  bool findNext(int minDoc);

  // helper: push every child's current doc up to at least docId. returns
  // false once any child exhausts during the skip
  bool advanceAllTo(int docId);

 public:
  ISRAnd(const Index* idx, std::vector<std::unique_ptr<ISR>> kids);

  bool Next() override;
  bool Seek(int location) override;

  int GetStartLocation() const override { return currentStart; }
  int GetEndLocation() const override { return currentEnd; }
  bool IsValid() const override { return valid; }
  int GetCurrentDocId() const override { return currentDoc; }
};

// phrase: children must appear at consecutive positions, all in the same
// field (same decoration). match start = first child's start, end = last
// child's end. typically constructed over ISRWord children
class ISRPhrase : public ISR {
 private:
  std::vector<std::unique_ptr<ISR>> children;
  int currentStart;
  int currentEnd;
  char currentDecoration;
  bool valid;

  // try to build a phrase from child[0]'s current position. if the tail
  // doesn't line up, advance child[0] and retry. returns false when
  // child[0] exhausts
  bool findFromCurrent();

 public:
  ISRPhrase(const Index* idx, std::vector<std::unique_ptr<ISR>> kids);

  bool Next() override;
  bool Seek(int location) override;

  int GetStartLocation() const override { return currentStart; }
  int GetEndLocation() const override { return currentEnd; }
  bool IsValid() const override { return valid; }
  char GetCurrentDecoration() const override { return currentDecoration; }
};

// disjunction: emits the child match with the smallest start. next
// advances only the child that produced the current match
class ISROr : public ISR {
 private:
  std::vector<std::unique_ptr<ISR>> children;
  int currentStart;
  int currentEnd;
  int currentChild;
  bool valid;

  // pick whichever valid child has the smallest GetStartLocation; fills
  // currentStart/end/child. sets valid=false if none remain
  void pickMin();

 public:
  ISROr(const Index* idx, std::vector<std::unique_ptr<ISR>> kids);

  bool Next() override;
  bool Seek(int location) override;

  int GetStartLocation() const override { return currentStart; }
  int GetEndLocation() const override { return currentEnd; }
  bool IsValid() const override { return valid; }
};

// container: emits one match per doc that contains every "contained"
// child AND no "excluded" child. handles NOT semantics. start/end span
// the contained children's first hits in the doc
class ISRContainer : public ISR {
 private:
  std::vector<std::unique_ptr<ISR>> contained;
  std::vector<std::unique_ptr<ISR>> excluded;
  ISREndDoc docs;
  int currentStart;
  int currentEnd;
  int currentDoc;
  bool valid;

  // find next doc >= minDoc where every contained child matches and no
  // excluded child matches. returns false when exhausted
  bool findNext(int minDoc);

  // align all contained children to the given doc; returns the first doc
  // >= target where every contained child lands, or -1 on exhaustion
  int converge(int target);

  // does any excluded child have a hit in docId?
  bool anyExcludedIn(int docId);

 public:
  ISRContainer(const Index* idx,
               std::vector<std::unique_ptr<ISR>> containedKids,
               std::vector<std::unique_ptr<ISR>> excludedKids);

  bool Next() override;
  bool Seek(int location) override;

  int GetStartLocation() const override { return currentStart; }
  int GetEndLocation() const override { return currentEnd; }
  bool IsValid() const override { return valid; }
  int GetCurrentDocId() const override { return currentDoc; }
};
