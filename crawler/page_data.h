#pragma once

#include <string>
#include <vector>

// data related to a single page
struct PageData {
    u_int64_t distance_from_seedlist;
    std::string url;
    std::vector<std::string> words, titlewords;
};

// a serialized page consists of a header followed by
// data containing the url, words, and titlewords
struct __attribute__((packed)) SerializedPageDataHeader {
    u_int64_t distance_from_seedlist;
};

// a page file consists of a page_file_header followed by num_pages page_data entries
struct __attribute__((packed)) PageFileHeader {
    u_int64_t magic_number;
    u_int64_t size_bytes;
    u_int64_t num_pages;
};

struct PageFile {
    u_int64_t size_bytes;
    u_int64_t num_pages;
    std::vector<std::vector<u_int8_t>> page_data_entries; // stored as serialized data in the vectors' data
};

// load given page file into memory for later use with get_next_page
// automatically closes previous page file if loaded
//
// Parameters:
//      file_name - name of file to be loaded
//
// Returns:
//      0 on success, -1 on failure
int load_page_file(const std::string& file_name);

// closes the current page file if loaded
void close_page_file();

// loads the next page's data
// a page file should be loaded using load_page_file before calling get_next_page
//
// Parameters:
//      pd - page_data struct to be written into
//
// Returns:
//      0 on success, -1 on failure
int get_next_page(PageData& pd);

// writes a string to buffer and increments buffer past the end of the serialized string
void serialize_string(void** buffer, const std::string& s);

// reads serialized string from buffer and increments buffer past the end of the string
std::string deserialize_string(void** buffer);

// writes a vector of strings to buffer and increments buffer past the end of the vector
void serialize_string_vector(void** buffer, const std::vector<std::string>& v);

// reads a vector of strings from buffer and increments buffer past the end of the vector
std::vector<std::string> deserialize_string_vector(void** buffer);