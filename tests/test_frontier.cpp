#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "crawler/frontier.h"

using Catch::Matchers::WithinAbs;

TEST_CASE("blacklist", "[crawler]") {
    std::string test_string = "https://google.com/a/b";
    std::string test_string_subset = test_string + "/c";
    std::string test_string_not_matching = "https://google.com";

    blacklist_url(test_string);
    REQUIRE(is_in_blacklist(test_string));
    REQUIRE(is_in_blacklist(test_string_subset));
    REQUIRE(!is_in_blacklist(test_string_not_matching));
}