#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ranker/dynamic_ranker.hpp"
#include "ranker/weight_profiles.hpp"

using Catch::Matchers::WithinAbs;

TEST_CASE("build_positions finds each query term in the field", "[dynamic_ranker]") {
  std::vector<std::string> query = {"python", "tutorial"};
  std::vector<std::string> field = {"learn", "python", "tutorial", "today"};

  auto positions = build_positions(query, field);

  REQUIRE(positions.size() == 2);
  REQUIRE(positions[0] == std::vector<size_t>{1});
  REQUIRE(positions[1] == std::vector<size_t>{2});
}

TEST_CASE("build_positions leaves missing terms empty", "[dynamic_ranker]") {
  std::vector<std::string> query = {"python", "rust"};
  std::vector<std::string> field = {"learn", "python", "today"};

  auto positions = build_positions(query, field);

  REQUIRE(positions.size() == 2);
  REQUIRE(positions[0] == std::vector<size_t>{1});
  REQUIRE(positions[1].empty());
}

TEST_CASE("exact phrase in title gives max T2 score", "[dynamic_ranker]") {
  DocCandidate doc{.title_words = {"python", "tutorial"}};
  std::vector<std::string> query = {"python", "tutorial"};

  REQUIRE_THAT(t2_span(query, doc), WithinAbs(1.0, 1e-9));
}

TEST_CASE("missing query term in title gives T2 = 0", "[dynamic_ranker]") {
  DocCandidate doc{.title_words = {"learn", "python", "today"}};
  std::vector<std::string> query = {"python", "rust"};

  REQUIRE(t2_span(query, doc) == 0.0);
}

TEST_CASE("exact phrase scores higher than scattered match", "[dynamic_ranker]") {
  DocCandidate exact{.title_words = {"python", "tutorial"}};
  DocCandidate scattered{.title_words = {"python", "is", "a", "language", "tutorial"}};
  std::vector<std::string> query = {"python", "tutorial"};

  REQUIRE(t2_span(query, exact) > t2_span(query, scattered));
}

TEST_CASE("reverse order scores lower than in-order", "[dynamic_ranker]") {
  DocCandidate forward{.title_words = {"python", "tutorial"}};
  DocCandidate reversed{.title_words = {"tutorial", "python"}};
  std::vector<std::string> query = {"python", "tutorial"};

  REQUIRE(t2_span(query, forward) > t2_span(query, reversed));
}

TEST_CASE("single term present scores max T2", "[dynamic_ranker]") {
  DocCandidate doc{.title_words = {"python"}};
  std::vector<std::string> query = {"python"};

  REQUIRE_THAT(t2_span(query, doc), WithinAbs(1.0, 1e-9));
}

TEST_CASE("empty query gives T2 = 0", "[dynamic_ranker]") {
  DocCandidate doc{.title_words = {"python", "tutorial"}};
  std::vector<std::string> query = {};

  REQUIRE(t2_span(query, doc) == 0.0);
}

TEST_CASE("score_dynamic stays in [0, 1] for any profile", "[dynamic_ranker]") {
  DocCandidate doc{.title_words = {"python", "tutorial"}};
  std::vector<std::string> query = {"python", "tutorial"};

  for (const auto& profile : {GENERAL, NAVIGATIONAL}) {
    double s = score_dynamic(query, doc, profile);
    REQUIRE(s >= 0.0);
    REQUIRE(s <= 1.0);
  }
}

TEST_CASE("score_dynamic uses profile weights", "[dynamic_ranker]") {
  // with T1, T3, T4 stubbed at 0.5 and T2 real, swapping profiles should
  // change the score — different weights on the same four numbers.
  DocCandidate doc{.title_words = {"python", "tutorial"}};
  std::vector<std::string> query = {"python", "tutorial"};

  double g = score_dynamic(query, doc, GENERAL);
  double n = score_dynamic(query, doc, NAVIGATIONAL);

  REQUIRE(g != n);
}
