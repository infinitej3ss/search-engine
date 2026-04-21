// isr.cpp
#include "isr.h"

#include <algorithm>
#include <climits>

// base ISR helpers

// binary search the doc-range table for the doc containing the current
// match's start position. used by boolean / constraint walkers
int ISR::GetCurrentDocId() const {
  if (!IsValid()) return -1;
  int pos = GetStartLocation();

  int left = 0;
  int right = index->GetDocumentCount() - 1;
  while (left <= right) {
    int mid = (left + right) / 2;
    int start_pos = 0, end_pos = 0;
    if (!index->GetDocumentRange(mid, start_pos, end_pos)) return -1;

    if (pos < start_pos) {
      right = mid - 1;
    } else if (pos > end_pos) {
      left = mid + 1;
    } else {
      return mid;
    }
  }
  return -1;
}

bool ISR::SkipToDoc(int targetDocId) {
  if (targetDocId < 0 || targetDocId >= index->GetDocumentCount()) return false;

  int start_pos = 0, end_pos = 0;
  if (!index->GetDocumentRange(targetDocId, start_pos, end_pos)) return false;
  return Seek(start_pos);
}

ISRWord::ISRWord(const Index* idx, const std::string& term)
    : ISR(idx), currentPostIdx(0), currentAbsolutePos(0) {
  postingList = idx->getPostingList(term);
  if (IsValid()) {
    currentAbsolutePos = index->decodeDelta(postingList.posts[0]);
  }
}

// move to the next non-EOD post
bool ISRWord::Next() {
  if (!IsValid()) return false;

  do {
    if (currentPostIdx + 1 >= static_cast<int>(postingList.posts.size())) {
      currentPostIdx = static_cast<int>(postingList.posts.size());
      return false;
    }
    currentPostIdx++;
    currentAbsolutePos += index->decodeDelta(postingList.posts[currentPostIdx]);
  } while (index->decodeDecoration(postingList.posts[currentPostIdx]) == '%');

  return true;
}

bool ISRWord::Seek(int location) {
  if (!IsValid()) return false;

  int checkpoint_abs = 0;
  int checkpoint_inx = 0;
  bool has_checkpoint =
      postingList.findCheckpoint(location, checkpoint_abs, checkpoint_inx);

  if (has_checkpoint) {
    currentPostIdx = checkpoint_inx;
    currentAbsolutePos = checkpoint_abs;
  } else {
    // no checkpoint: start at post 0 with its delta (matches constructor init)
    currentPostIdx = 0;
    currentAbsolutePos = index->decodeDelta(postingList.posts[0]);
  }

  while (IsValid() && currentAbsolutePos < location) {
    if (currentPostIdx + 1 >= static_cast<int>(postingList.posts.size())) {
      currentPostIdx = static_cast<int>(postingList.posts.size());
      return false;
    }
    currentPostIdx++;
    currentAbsolutePos += index->decodeDelta(postingList.posts[currentPostIdx]);
  }

  // skip past any EOD posts — doc boundaries are shared slots, so Seek can
  // land on the previous doc's EOD when target == shared boundary position
  while (IsValid() &&
         index->decodeDecoration(postingList.posts[currentPostIdx]) == '%') {
    if (currentPostIdx + 1 >= static_cast<int>(postingList.posts.size())) {
      currentPostIdx = static_cast<int>(postingList.posts.size());
      return false;
    }
    currentPostIdx++;
    currentAbsolutePos += index->decodeDelta(postingList.posts[currentPostIdx]);
  }

  return IsValid() && currentAbsolutePos >= location;
}

bool ISRWord::IsValid() const {
  return postingList.is_valid &&
         currentPostIdx < static_cast<int>(postingList.posts.size());
}

char ISRWord::GetCurrentDecoration() const {
  if (!IsValid()) return -1;
  return index->decodeDecoration(postingList.posts[currentPostIdx]);
}

ISREndDoc::ISREndDoc(const Index* idx)
    : ISR(idx),
      numDocs(idx->GetDocumentCount()),
      currentDoc(0),
      currentStart(0),
      currentEnd(0) {
  if (numDocs <= 0) {
    currentDoc = -1;
    return;
  }
  if (!idx->GetDocumentRange(0, currentStart, currentEnd)) currentDoc = -1;
}

bool ISREndDoc::Next() {
  if (currentDoc < 0 || currentDoc >= numDocs) return false;
  currentDoc++;
  if (currentDoc >= numDocs) return false;
  return index->GetDocumentRange(currentDoc, currentStart, currentEnd);
}

// position on the earliest doc whose end position is >= location. a
// location that falls in a gap between docs lands on the following doc
bool ISREndDoc::Seek(int location) {
  int lo = 0, hi = numDocs - 1;
  int candidate = -1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int s = 0, e = 0;
    if (!index->GetDocumentRange(mid, s, e)) return false;
    if (location <= e) {
      candidate = mid;
      hi = mid - 1;
    } else {
      lo = mid + 1;
    }
  }
  if (candidate < 0) {
    currentDoc = numDocs;
    return false;
  }
  currentDoc = candidate;
  return index->GetDocumentRange(currentDoc, currentStart, currentEnd);
}

bool ISREndDoc::SkipToDocContaining(int location) {
  return Seek(location);
}

ISRAnd::ISRAnd(const Index* idx, std::vector<std::unique_ptr<ISR>> kids)
    : ISR(idx),
      children(std::move(kids)),
      docs(idx),
      currentStart(0),
      currentEnd(0),
      currentDoc(-1),
      valid(false) {
  if (children.empty()) return;
  findNext(0);
}

// align every child to doc >= minDoc and emit the first doc where all
// children coincide. returns false if any child exhausts first
bool ISRAnd::findNext(int minDoc) {
  if (children.empty()) { valid = false; return false; }

  int target = minDoc;
  while (true) {
    for (auto& c : children) {
      if (!c->IsValid()) { valid = false; return false; }
      int cd = c->GetCurrentDocId();
      if (cd < 0) { valid = false; return false; }
      if (cd < target) {
        if (!c->SkipToDoc(target)) { valid = false; return false; }
        cd = c->GetCurrentDocId();
        if (cd < 0) { valid = false; return false; }
      }
    }

    int maxDoc = children[0]->GetCurrentDocId();
    for (size_t i = 1; i < children.size(); i++) {
      int d = children[i]->GetCurrentDocId();
      if (d > maxDoc) maxDoc = d;
    }

    if (maxDoc == target) {
      currentDoc = target;
      int s = INT_MAX, e = INT_MIN;
      for (auto& c : children) {
        s = std::min(s, c->GetStartLocation());
        e = std::max(e, c->GetEndLocation());
      }
      currentStart = s;
      currentEnd = e;
      valid = true;
      return true;
    }
    target = maxDoc;
  }
}

bool ISRAnd::advanceAllTo(int docId) {
  for (auto& c : children) {
    if (!c->IsValid()) return false;
    if (c->GetCurrentDocId() < docId) {
      if (!c->SkipToDoc(docId)) return false;
    }
  }
  return true;
}

bool ISRAnd::Next() {
  if (!valid) return false;
  return findNext(currentDoc + 1);
}

bool ISRAnd::Seek(int location) {
  if (!docs.SkipToDocContaining(location)) { valid = false; return false; }
  return findNext(docs.GetCurrentDocIndex());
}

ISROr::ISROr(const Index* idx, std::vector<std::unique_ptr<ISR>> kids)
    : ISR(idx),
      children(std::move(kids)),
      currentStart(0),
      currentEnd(0),
      currentChild(-1),
      valid(false) {
  pickMin();
}

// pick whichever valid child has the smallest GetStartLocation. ties go
// to the earliest child in the list
void ISROr::pickMin() {
  currentChild = -1;
  int bestStart = INT_MAX;
  for (size_t i = 0; i < children.size(); i++) {
    if (!children[i]->IsValid()) continue;
    int s = children[i]->GetStartLocation();
    if (s < bestStart) {
      bestStart = s;
      currentChild = static_cast<int>(i);
    }
  }
  if (currentChild < 0) {
    valid = false;
    return;
  }
  valid = true;
  currentStart = children[currentChild]->GetStartLocation();
  currentEnd = children[currentChild]->GetEndLocation();
}

bool ISROr::Next() {
  if (!valid) return false;
  children[currentChild]->Next();
  pickMin();
  return valid;
}

bool ISROr::Seek(int location) {
  for (auto& c : children) {
    if (!c->IsValid()) continue;
    if (c->GetStartLocation() < location) c->Seek(location);
  }
  pickMin();
  return valid;
}

ISRPhrase::ISRPhrase(const Index* idx, std::vector<std::unique_ptr<ISR>> kids)
    : ISR(idx),
      children(std::move(kids)),
      currentStart(0),
      currentEnd(0),
      currentDecoration(0),
      valid(false) {
  if (children.empty()) return;
  findFromCurrent();
}

// try to extend a phrase starting at child[0]'s current position. if the
// tail doesn't align (position mismatch or different decoration), advance
// child[0] and retry. bails when child[0] exhausts
bool ISRPhrase::findFromCurrent() {
  if (children.empty()) { valid = false; return false; }

  while (children[0]->IsValid()) {
    int anchor = children[0]->GetStartLocation();
    char dec = children[0]->GetCurrentDecoration();

    bool ok = true;
    for (size_t i = 1; i < children.size(); i++) {
      int want = anchor + static_cast<int>(i);
      if (!children[i]->Seek(want)) { ok = false; break; }
      if (children[i]->GetStartLocation() != want) { ok = false; break; }
      if (dec != 0 && children[i]->GetCurrentDecoration() != dec) {
        ok = false; break;
      }
    }

    if (ok) {
      currentStart = anchor;
      currentEnd = anchor + static_cast<int>(children.size()) - 1;
      currentDecoration = dec;
      valid = true;
      return true;
    }

    if (!children[0]->Next()) break;
  }

  valid = false;
  return false;
}

bool ISRPhrase::Next() {
  if (!valid) return false;
  if (!children[0]->Next()) { valid = false; return false; }
  return findFromCurrent();
}

bool ISRPhrase::Seek(int location) {
  if (children.empty()) { valid = false; return false; }
  if (!children[0]->Seek(location)) { valid = false; return false; }
  return findFromCurrent();
}

ISRContainer::ISRContainer(const Index* idx,
                           std::vector<std::unique_ptr<ISR>> containedKids,
                           std::vector<std::unique_ptr<ISR>> excludedKids)
    : ISR(idx),
      contained(std::move(containedKids)),
      excluded(std::move(excludedKids)),
      docs(idx),
      currentStart(0),
      currentEnd(0),
      currentDoc(-1),
      valid(false) {
  if (contained.empty()) return;
  findNext(0);
}

// align contained children to >= target and report the doc where every
// one lands. returns -1 on exhaustion
int ISRContainer::converge(int target) {
  while (true) {
    for (auto& c : contained) {
      if (!c->IsValid()) return -1;
      int cd = c->GetCurrentDocId();
      if (cd < 0) return -1;
      if (cd < target) {
        if (!c->SkipToDoc(target)) return -1;
        cd = c->GetCurrentDocId();
        if (cd < 0) return -1;
      }
    }
    int maxDoc = contained[0]->GetCurrentDocId();
    for (size_t i = 1; i < contained.size(); i++) {
      int d = contained[i]->GetCurrentDocId();
      if (d > maxDoc) maxDoc = d;
    }
    if (maxDoc == target) return target;
    target = maxDoc;
  }
}

bool ISRContainer::anyExcludedIn(int docId) {
  for (auto& x : excluded) {
    if (!x->IsValid()) continue;
    int xd = x->GetCurrentDocId();
    if (xd < 0) continue;
    if (xd < docId) {
      if (!x->SkipToDoc(docId)) continue;
      xd = x->GetCurrentDocId();
      if (xd < 0) continue;
    }
    if (xd == docId) return true;
  }
  return false;
}

bool ISRContainer::findNext(int minDoc) {
  if (contained.empty()) { valid = false; return false; }

  int target = minDoc;
  while (true) {
    int doc = converge(target);
    if (doc < 0) { valid = false; return false; }

    if (!anyExcludedIn(doc)) {
      currentDoc = doc;
      int s = INT_MAX, e = INT_MIN;
      for (auto& c : contained) {
        s = std::min(s, c->GetStartLocation());
        e = std::max(e, c->GetEndLocation());
      }
      currentStart = s;
      currentEnd = e;
      valid = true;
      return true;
    }

    // excluded child blocks this doc; skip past it and retry
    target = doc + 1;
  }
}

bool ISRContainer::Next() {
  if (!valid) return false;
  return findNext(currentDoc + 1);
}

bool ISRContainer::Seek(int location) {
  if (!docs.SkipToDocContaining(location)) { valid = false; return false; }
  return findNext(docs.GetCurrentDocIndex());
}
