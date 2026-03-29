#include "string"

// 
enum get_url_return {
    failure,
    blacklist,
    success
};


// take a URL and return the HTML
get_url_return get_ssl(std::string &page){

    // TODO: implement functionality

    page = "<!DOCTYPE html><html><head></head><body>Hello world!</body></html>";

    return success;
}