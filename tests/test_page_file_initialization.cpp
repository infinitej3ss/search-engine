#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "crawler/page_data.h"

using Catch::Matchers::WithinAbs;

TEST_CASE("initialize dirs for writing", "[crawler]") {
    int result = initialize_page_file_dir("../crawler_test_files/");
    REQUIRE(result != -1);

    REQUIRE(PAGE_FILES[0].num_files_of_this_rank_written == 1);
}

TEST_CASE("initialize dirs for reading", "[crawler]") {
    std::vector<std::vector<std::string>> result = get_page_file_names("../crawler_test_files/");
    REQUIRE(result[0].size() > 0);

    REQUIRE(result[0][0] == "../crawler_test_files/crawled_page_data_rank_0_num_0");
}