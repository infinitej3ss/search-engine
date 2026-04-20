// isr.cpp
#include "isr.h"

ISR::ISR(const Index* idx, const std::string& term)
    : index(idx), currentPostIdx(0), currentAbsolutePos(0) {
  postingList = idx->getPostingList(term);
  if (IsValid()) {
    currentAbsolutePos = index->decodeDelta(postingList.posts[0]);
  }
}

// Move to the next non-EOD post.
bool ISR::Next() {
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

bool ISR::Seek(int location) {
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

// Binary search on doc ranges to find which doc contains currentAbsolutePos.
// Uses the lightweight range accessor to avoid materializing title_words
// etc. on every probe.
int ISR::GetCurrentDocId() const {
  if (!IsValid()) return -1;

  int left = 0;
  int right = index->GetDocumentCount() - 1;

  while (left <= right) {
    int mid = (left + right) / 2;
    int start_pos, end_pos;
    if (!index->GetDocumentRange(mid, start_pos, end_pos)) return -1;

    if (currentAbsolutePos < start_pos) {
      right = mid - 1;
    } else if (currentAbsolutePos > end_pos) {
      left = mid + 1;
    } else {
      return mid;
    }
  }
  return -1;
}

bool ISR::SkipToDoc(int targetDocId) {
  if (!IsValid()) return false;
  if (targetDocId < 0 || targetDocId >= index->GetDocumentCount()) return false;

  int start_pos, end_pos;
  if (!index->GetDocumentRange(targetDocId, start_pos, end_pos)) return false;
  return Seek(start_pos);
}

bool ISR::IsValid() const {
  return postingList.is_valid &&
         currentPostIdx < static_cast<int>(postingList.posts.size());
}

int ISR::GetCurrentPos() const { return currentAbsolutePos; }

char ISR::GetCurrentDecoration() const {
  if (!IsValid()) return -1;
  return index->decodeDecoration(postingList.posts[currentPostIdx]);
}
