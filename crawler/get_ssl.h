#pragma once

#include "string"

enum get_ssl_return {
    failure,
    blacklist,
    success
};

// take a URL and return the HTML
get_ssl_return get_ssl(std::string& url, std::string& page);