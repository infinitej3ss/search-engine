#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ranker/config/weights.hpp"
#include "ranker/dynamic/dynamic_ranker.hpp"
#include "ranker/static/static_ranker.hpp"

// stand-in corpus until the crawler/index are wired up
static const std::vector<DocCandidate> CORPUS = {
  {0, "https://docs.python.org/3/tutorial/", {"the","python","tutorial"}, {"python","docs"}, 1, ""},
  {0, "https://www.python.org/", {"welcome","to","python","org"}, {"python"}, 0, ""},
  {0, "https://realpython.com/python-first-steps/", {"python","first","steps"}, {"python","beginners"}, 2, ""},
  {0, "https://wiki.python.org/moin/BeginnersGuide", {"beginners","guide","python","wiki"}, {"beginner","python"}, 2, ""},
  {0, "https://en.cppreference.com/w/cpp", {"c++","reference"}, {"cpp","stdlib","reference"}, 1, ""},
  {0, "https://isocpp.org/", {"standard","c++"}, {"c++","iso"}, 1, ""},
  {0, "https://en.cppreference.com/w/cpp/language/tutorial", {"c++","language","tutorial"}, {"cpp","tutorial"}, 2, ""},
  {0, "https://scikit-learn.org/stable/", {"scikit-learn","machine","learning","in","python"}, {"sklearn","machine","learning"}, 1, ""},
  {0, "https://www.tensorflow.org/tutorials", {"tensorflow","tutorials"}, {"tensorflow","tutorial","ml"}, 1, ""},
  {0, "https://pytorch.org/tutorials/", {"welcome","to","pytorch","tutorials"}, {"pytorch","tutorial"}, 1, ""},
  {0, "https://en.wikipedia.org/wiki/Machine_learning", {"machine","learning","wikipedia"}, {"ml","wiki"}, 2, ""},
  {0, "https://arxiv.org/abs/1706.03762", {"attention","is","all","you","need"}, {"transformer","paper","attention"}, 3, ""},
  {0, "https://example.tk/get-rich-python-tutorial-FREE", {"free","python","tutorial","click","now"}, {}, 5, ""},
  {0, "http://192.168.0.1/python/tutorial.html", {"python","tutorial"}, {}, 6, ""},
  {0, "https://stackoverflow.com/questions/tagged/python", {"newest","python","questions","stack","overflow"}, {"python","stackoverflow"}, 2, ""},
};

static std::vector<std::string> tokenize(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  std::vector<std::string> out;
  std::istringstream iss(s);
  for (std::string t; iss >> t;) out.push_back(t);
  return out;
}

static double score(const std::vector<std::string>& q, const DocCandidate& d) {
  RankerInput in{d.url, true, 0, static_cast<size_t>(std::max(d.hop_distance, 0)), 0, 0.0};
  double s = StaticRanker(in).rank();
  if (s < 0.0) return -1.0;
  return s * score_dynamic(q, d, GENERAL);
}

int main() {
  load_and_apply_weights("weights.txt");
  std::cout << "demo (" << CORPUS.size() << " docs) — Ctrl+D to quit\n";

  std::string line;
  while (std::cout << "> " && std::getline(std::cin, line)) {
    auto q = tokenize(line);
    if (q.empty()) continue;
    load_and_apply_weights("weights.txt");

    std::vector<std::pair<double, size_t>> hits;
    for (size_t i = 0; i < CORPUS.size(); i++) {
      double v = score(q, CORPUS[i]);
      if (v > 0.0) hits.push_back({v, i});
    }
    std::sort(hits.begin(), hits.end(), std::greater<>{});

    for (size_t i = 0; i < std::min<size_t>(hits.size(), 10); i++) {
      std::printf("%2zu. [%.4f] %s\n", i + 1, hits[i].first,
                  CORPUS[hits[i].second].url.c_str());
    }
  }
}
