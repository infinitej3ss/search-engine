#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "engine/search_engine.hpp"
#include "index.h"
#include "index_builder.h"
#include "page_data.h"

namespace {

// build a minimal page file on disk with known pages, and return the
// byte offset of each page as reported by get_next_page during iteration
std::vector<std::pair<int, PageData>> write_and_iterate(
    const std::string& path, const std::vector<PageData>& pages) {

  std::vector<std::vector<u_int8_t>> blobs;
  u_int64_t total_size = 0;
  for (const auto& pd : pages) {
    u_int64_t sz = sizeof(u_int64_t); // distance_from_seedlist
    sz += sizeof(u_int16_t) + pd.url.size();
    sz += sizeof(u_int64_t);
    for (const auto& w : pd.words) sz += sizeof(u_int16_t) + w.size();
    sz += sizeof(u_int64_t);
    for (const auto& w : pd.titlewords) sz += sizeof(u_int16_t) + w.size();
    sz += sizeof(u_int64_t);
    for (const auto& w : pd.anchor_text) sz += sizeof(u_int16_t) + w.size();

    std::vector<u_int8_t> buf(sz);
    void* p = buf.data();
    std::memcpy(p, &pd.distance_from_seedlist, sizeof(u_int64_t));
    p = (u_int8_t*)p + sizeof(u_int64_t);
    serialize_string(&p, pd.url);
    serialize_string_vector(&p, pd.words);
    serialize_string_vector(&p, pd.titlewords);
    serialize_string_vector(&p, pd.anchor_text);
    blobs.push_back(std::move(buf));
    total_size += blobs.back().size();
  }

  PageFileHeader hdr;
  hdr.magic_number = CORRECT_MAGIC_NUMBER;
  hdr.size_bytes = total_size;
  hdr.num_pages = pages.size();

  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  for (const auto& b : blobs) f.write(reinterpret_cast<const char*>(b.data()), b.size());
  f.close();

  std::vector<std::pair<int, PageData>> captured;
  REQUIRE(load_page_file(path) == 0);
  PageData pd;
  int idx;
  while ((idx = get_next_page(pd)) != -1) {
    captured.push_back({idx, pd});
  }
  close_page_file();
  REQUIRE(captured.size() == pages.size());
  return captured;
}

}  // namespace

TEST_CASE("get_page_data_from_index round-trips with get_next_page's offsets",
          "[page_data][snippet]") {
  const std::string dir = "test_snippet_tmp/";
  std::filesystem::create_directories(dir);
  const std::string path = dir + "crawled_page_data_rank_0_num_0";

  PageData a;
  a.url = "https://ex.com/a"; a.distance_from_seedlist = 1;
  a.titlewords = {"alpha", "page"};
  a.words = {"hello", "world", "this", "is", "page", "a", "and", "it", "has", "content"};

  PageData b;
  b.url = "https://ex.com/b"; b.distance_from_seedlist = 2;
  b.titlewords = {"beta"};
  b.words = {"second", "page", "body", "text", "goes", "here"};

  PageData c;
  c.url = "https://ex.com/c"; c.distance_from_seedlist = 3;
  c.titlewords = {"gamma"};
  c.words = {"third"};

  auto captured = write_and_iterate(path, {a, b, c});

  for (size_t i = 0; i < captured.size(); i++) {
    const auto& [offset, expected] = captured[i];
    PageData pd;
    CAPTURE(i, offset);
    REQUIRE(get_page_data_from_index(pd, dir, 0, 0, static_cast<u_int64_t>(offset)) == 0);
    REQUIRE(pd.url == expected.url);
    REQUIRE(pd.words == expected.words);
    REQUIRE(pd.titlewords == expected.titlewords);
    REQUIRE(pd.distance_from_seedlist == expected.distance_from_seedlist);
  }

  std::filesystem::remove_all(dir);
}

TEST_CASE("SearchEngine::search fills snippet from page file",
          "[search_engine][snippet]") {
  const std::string dir = "test_snippet_engine_tmp/";
  std::filesystem::create_directories(dir);

  // write a page file with one page that contains "foo" at a known position
  PageData pd;
  pd.url = "https://ex.com/target";
  pd.distance_from_seedlist = 1;
  pd.titlewords = {"target", "page"};
  pd.words = {"lorem", "ipsum", "and", "here", "is", "foo", "in", "the", "middle",
              "of", "some", "body", "text"};

  auto captured = write_and_iterate(dir + "crawled_page_data_rank_0_num_0", {pd});
  REQUIRE(captured.size() == 1);

  // build index pointing at that page, capturing rank/num/index
  IndexBuilder idx;
  pd.page_file_rank = 0;
  pd.page_file_num = 0;
  pd.page_file_index = static_cast<u_int64_t>(captured[0].first);
  idx.addDocument(pd);
  idx.Finalize();
  REQUIRE(idx.WriteBlobV4(dir + "index_rank_0.blob"));

  // search the engine and verify the snippet mentions "foo"
  // use the real weights file so scores aren't all zero (compute_score
  // filters out s <= 0 results, so without weights we'd drop every match)
  SearchEngine engine("../config/weights.txt", dir);
  int total = 0;
  auto results = engine.search("foo", 0, 10, &total);
  REQUIRE(total == 1);
  REQUIRE(results.size() == 1);
  CAPTURE(results[0].snippet);
  REQUIRE(results[0].snippet.find("foo") != std::string::npos);

  std::filesystem::remove_all(dir);
}
