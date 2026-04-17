#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "index/index.h"
#include "index/page_data.h"
#include "index/constraint_solver.h"
#include "query/query_compiler.hpp"
#include "../config/weights.hpp"
#include "ranker/dynamic/dynamic_ranker.hpp"
#include "ranker/static/static_ranker.hpp"

static constexpr int TOP_K = 10;

// Fallback corpus used when no `index_rank_*.blob` files are present.
// Handy for demoing the pipeline before the crawler has produced real data.
static std::vector<PageData> make_demo_corpus() {
  auto mk = [](uint64_t hop, const std::string& url,
               std::vector<std::string> title,
               std::vector<std::string> anchor) {
    PageData p;
    p.distance_from_seedlist = hop;
    p.url = url;
    p.titlewords = title;
    p.words = title;
    p.anchor_text = std::move(anchor);
    return p;
  };
  return {
    mk(1, "https://docs.python.org/3/tutorial/",                {"the","python","tutorial"},                     {"python","docs"}),
    mk(0, "https://www.python.org/",                            {"welcome","to","python","org"},                 {"python"}),
    mk(2, "https://realpython.com/python-first-steps/",         {"python","first","steps"},                      {"python","beginners"}),
    mk(2, "https://wiki.python.org/moin/BeginnersGuide",        {"beginners","guide","python","wiki"},           {"beginner","python"}),
    mk(1, "https://en.cppreference.com/w/cpp",                  {"c","reference"},                               {"cpp","stdlib","reference"}),
    mk(1, "https://isocpp.org/",                                {"standard","c"},                                {"c","iso"}),
    mk(2, "https://en.cppreference.com/w/cpp/language/tutorial",{"c","language","tutorial"},                     {"cpp","tutorial"}),
    mk(1, "https://scikit-learn.org/stable/",                   {"scikit","learn","machine","learning","in","python"}, {"sklearn","machine","learning"}),
    mk(1, "https://www.tensorflow.org/tutorials",               {"tensorflow","tutorials"},                      {"tensorflow","tutorial","ml"}),
    mk(1, "https://pytorch.org/tutorials/",                     {"welcome","to","pytorch","tutorials"},          {"pytorch","tutorial"}),
    mk(2, "https://en.wikipedia.org/wiki/Machine_learning",     {"machine","learning","wikipedia"},              {"ml","wiki"}),
    mk(3, "https://arxiv.org/abs/1706.03762",                   {"attention","is","all","you","need"},           {"transformer","paper","attention"}),
    mk(5, "https://example.tk/get-rich-python-tutorial-FREE",   {"free","python","tutorial","click","now"},      {}),
    mk(6, "http://192.168.0.1/python/tutorial.html",            {"python","tutorial"},                           {}),
    mk(2, "https://stackoverflow.com/questions/tagged/python",  {"newest","python","questions","stack","overflow"}, {"python","stackoverflow"}),
  };
}

static DocCandidate to_candidate(const Index::DocumentMetadata& meta) {
  DocCandidate d;
  d.doc_id = static_cast<uint32_t>(meta.doc_id);
  d.url = meta.url;
  d.title_words = meta.title_words;
  d.body_words = meta.body_words;
  d.anchor_texts = meta.anchor_texts;
  d.hop_distance = meta.hop_distance;
  return d;
}

static double score(const std::vector<std::string>& q, const DocCandidate& d) {
  RankerInput in{d.url, true, 0, static_cast<size_t>(std::max(d.hop_distance, 0)), 0, 0.0};
  double s = StaticRanker(in).rank();
  if (s < 0.0) return -1.0;
  return s * score_dynamic(q, d, GENERAL);
}

struct Hit {
  double score;
  int rank;          // which rank bucket this doc came from
  std::string url;
};

// Try to load per-rank blobs. Returns the number of blobs loaded (0 means
// caller should fall back to the demo corpus).
static int load_rank_blobs(std::vector<std::unique_ptr<Index>>& out) {
  for (int rank = 0; rank < static_cast<int>(NUM_PAGE_FILE_RANKS); rank++) {
    std::string path = "index_rank_" + std::to_string(rank) + ".blob";
    if (!std::filesystem::exists(path)) continue;
    auto idx = std::make_unique<Index>();
    if (!idx->LoadBlob(path)) {
      std::fprintf(stderr, "failed to load %s\n", path.c_str());
      continue;
    }
    std::printf("loaded %s (%d docs)\n", path.c_str(), idx->GetDocumentCount());
    out.push_back(std::move(idx));
  }
  return static_cast<int>(out.size());
}

int main() {
  std::vector<std::unique_ptr<Index>> indexes;
  int loaded = load_rank_blobs(indexes);

  if (loaded == 0) {
    std::printf("no index_rank_*.blob found, using hardcoded demo corpus\n");
    auto idx = std::make_unique<Index>();
    for (const auto& page : make_demo_corpus()) idx->addDocument(page);
    idx->Finalize();
    indexes.push_back(std::move(idx));
  }

  load_and_apply_weights("weights.txt");

  int total_docs = 0;
  for (const auto& idx : indexes) total_docs += idx->GetDocumentCount();
  std::printf("ready: %zu rank(s), %d docs — Ctrl+D to quit\n",
              indexes.size(), total_docs);

  std::string line;
  while (std::cout << "> " && std::getline(std::cin, line)) {
    auto terms = query::compile(line);
    if (terms.empty()) continue;
    load_and_apply_weights("weights.txt");

    std::vector<Hit> hits;
    for (size_t r = 0; r < indexes.size(); r++) {
      ConstraintSolver solver(indexes[r].get());
      auto doc_ids = solver.FindAndQuery(terms);
      for (int doc_id : doc_ids) {
        auto meta = indexes[r]->GetDocumentMetadata(doc_id);
        DocCandidate cand = to_candidate(meta);
        double v = score(terms, cand);
        if (v > 0.0) hits.push_back({v, static_cast<int>(r), meta.url});
      }
      // Cascade: stop descending once we've accumulated enough hits.
      if (static_cast<int>(hits.size()) >= TOP_K) break;
    }

    // Sort by rank first (priority), then by score within a rank.
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
      if (a.rank != b.rank) return a.rank < b.rank;
      return a.score > b.score;
    });

    if (hits.empty()) { std::cout << "  (no matches)\n"; continue; }
    for (size_t i = 0; i < std::min<size_t>(hits.size(), TOP_K); i++) {
      std::printf("%2zu. [r%d %.4f] %s\n", i + 1, hits[i].rank, hits[i].score,
                  hits[i].url.c_str());
    }
  }
}
