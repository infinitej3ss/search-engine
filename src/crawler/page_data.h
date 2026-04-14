#pragma once

#include <pthread.h>

#include <string>
#include <vector>

// data related to a single page
struct PageData {
    u_int64_t distance_from_seedlist;
    std::string url;
    std::vector<std::string> words, titlewords, anchor_text;
};

// a page file consists of a page_file_header followed by num_pages page_data entries
struct __attribute__((packed)) PageFileHeader {
    u_int64_t magic_number;
    u_int64_t size_bytes;
    u_int64_t num_pages;
};

struct PageFile {
    pthread_mutex_t page_file_mutex = PTHREAD_MUTEX_INITIALIZER;
    u_int64_t num_files_of_this_rank_written = 0;
    u_int64_t size_bytes = 0;
    u_int64_t num_pages = 0;
    std::vector<std::vector<u_int8_t>> page_data_entries; // stored as serialized data in the vectors' data
};

const u_int64_t NUM_PAGE_FILE_RANKS = 10;                    // number of page files to be written to based on url's ranking
const u_int64_t MAX_PAGE_FILE_SIZE_BYTES = 50000000;        // 50MB
const u_int64_t CORRECT_MAGIC_NUMBER = 863404304674789781;  // magic number for page file

inline void* MAPPED_PAGE_FILE;
inline u_int64_t MAPPED_PAGE_FILE_SIZE = 0;
inline void* CURRENT_PAGE_FILE_LOCATION;
inline bool VALID_PAGE_FILE = false;
inline u_int64_t NUM_PAGE_FILE_ENTRIES = 0;
inline u_int64_t PAGE_FILE_SIZE_BYTES = 0;
inline pthread_mutex_t PAGE_FILE_MUTEX = PTHREAD_MUTEX_INITIALIZER;
inline pthread_mutex_t PAGE_FILE_INDEXING_MUTEX = PTHREAD_MUTEX_INITIALIZER;

inline u_int64_t NUM_CRAWLED_PAGES = 0;
inline pthread_mutex_t NUM_CRAWLED_MUTEX = PTHREAD_MUTEX_INITIALIZER;

inline PageFile PAGE_FILES[NUM_PAGE_FILE_RANKS];
inline std::string DIR_PATH = "./";  // TODO: function to initialize this path and page file's number written

// a serialized page consists of a header followed by
// data containing the url, words, and titlewords
struct __attribute__((packed)) SerializedPageDataHeader {
    u_int64_t distance_from_seedlist;
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
//      index of this page in the page file on success, -1 on failure
int get_next_page(PageData& pd);

// write page data to specified rank file
//
// Returns:
//      0 on success, -1 on failure, 1 if page is full and ready to be written
int write_page(u_int64_t rank_file, PageData& pd);

// write specified page file to disk
//
// Returns:
//      0 on success, -1 on failure
int write_page_file(u_int64_t rank_file);

// writes a string to buffer and increments buffer past the end of the serialized string
void serialize_string(void** buffer, const std::string& s);

// reads serialized string from buffer and increments buffer past the end of the string
std::string deserialize_string(void** buffer);

// writes a vector of strings to buffer and increments buffer past the end of the vector
void serialize_string_vector(void** buffer, const std::vector<std::string>& v);

// reads a vector of strings from buffer and increments buffer past the end of the vector
std::vector<std::string> deserialize_string_vector(void** buffer);

// initializes the page file directory specified for writing
//
// Parameters:
//      dir - directory to initialize from
//
// Returns:
//      0 on success, -1 on failure
int initialize_page_file_dir(const std::string& dir);

// get the names of page files in the specified directory
//
// Parameters:
//      dir - directory to parse
//
// Returns:
//      a 2D vector where the first index corresponds to rank and the second stores the file names
std::vector<std::vector<std::string>> get_page_file_names(const std::string& dir);

u_int64_t get_num_crawled_pages();

// find rank of a given page file name
//
// returns __INT64_MAX__ if the page is invalid
u_int64_t get_page_file_rank(const std::string& file_name);

// find num of a given page file name
//
// returns __INT64_MAX__ if the page is invalid
u_int64_t get_page_file_num(const std::string& file_name);

// loads page data for a specific page using an index
//
// Parameters:
//      pd - PageData struct to overwrite
//      dir - directory with the file
//      rank - rank of the file
//      num - num of the file
//      index - index of the page data in the file
//
// Returns:
//      0 on success, -1 on failure
int get_page_data_from_index(PageData& pd, const std::string& dir, const u_int64_t rank, const u_int64_t num, const u_int64_t index);

u_int64_t rank_bucket_from_double(const double score);