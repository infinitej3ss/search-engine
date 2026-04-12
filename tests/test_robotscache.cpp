#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "crawler/robots.txt/RobotsCache.h"

using Catch::Matchers::WithinAbs;

TEST_CASE("robots.txt cache", "[getSSL]"){
    RobotsCache rc;

    std::string robotsURL = "https://erowid.org";

    rc.request_robots_file(robotsURL);
}

TEST_CASE("domain_to_origin", "[getSSL]"){
    std::string s1 = "https://google.com";
    std::string s2 = "http://google.com";
    std::string s3 = "https://google.com/a/b/c";

    REQUIRE((domain_to_origin(s1) != domain_to_origin(s2)));
    REQUIRE((domain_to_origin(s1) == domain_to_origin(s3)));
}