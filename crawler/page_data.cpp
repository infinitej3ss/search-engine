#pragma once

#include "page_data.h"

#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>
#include <utility>

const u_int64_t CORRECT_MAGIC_NUMBER = 863404304674789781;

void* MAPPED_PAGE_FILE;
void* CURRENT_PAGE_FILE_LOCATION;
bool VALID_PAGE_FILE = false;
u_int64_t NUM_PAGE_FILE_ENTRIES = 0;
u_int64_t PAGE_FILE_SIZE_BYTES = 0;
pthread_mutex_t PAGE_FILE_MUTEX = PTHREAD_MUTEX_INITIALIZER;


// load given page file into memory for later use with get_next_page
int load_page_file(const std::string& file_name) {
    pthread_mutex_lock(&PAGE_FILE_MUTEX);

    // make sure any previous file is closed
    if (VALID_PAGE_FILE) {
        munmap(MAPPED_PAGE_FILE, PAGE_FILE_SIZE_BYTES);
        VALID_PAGE_FILE = false;
    }

    // open the file
    int fd = open(file_name.c_str(), O_RDONLY);
    if(fd == -1) {
        pthread_mutex_unlock(&PAGE_FILE_MUTEX);
        return -1;
    }

    // extract header
    PageFileHeader file_header;
    if(read(fd, &file_header, sizeof(PageFileHeader)) != sizeof(PageFileHeader)) {
        pthread_mutex_unlock(&PAGE_FILE_MUTEX);
        return -1;
    }

    // check magic number to make sure this is a page file
    if(file_header.magic_number != CORRECT_MAGIC_NUMBER) {
        pthread_mutex_unlock(&PAGE_FILE_MUTEX);
        return -1;
    }
    NUM_PAGE_FILE_ENTRIES = file_header.num_pages;
    PAGE_FILE_SIZE_BYTES = file_header.size_bytes;

    // create memory mapping
    MAPPED_PAGE_FILE, CURRENT_PAGE_FILE_LOCATION = mmap(NULL, PAGE_FILE_SIZE_BYTES, PROT_READ, 0, fd, 0);
    if(MAPPED_PAGE_FILE == MAP_FAILED) {
        pthread_mutex_unlock(&PAGE_FILE_MUTEX);
        return -1;
    }
    VALID_PAGE_FILE = true;
    pthread_mutex_unlock(&PAGE_FILE_MUTEX);
    return 0;
}

// closes the current page file if loaded
void close_page_file() {
    pthread_mutex_lock(&PAGE_FILE_MUTEX);
    if (VALID_PAGE_FILE) {
        munmap(MAPPED_PAGE_FILE, PAGE_FILE_SIZE_BYTES);
        VALID_PAGE_FILE = false;
    }
    pthread_mutex_unlock(&PAGE_FILE_MUTEX);
}

// loads the next page's data
int get_next_page(PageData& pd) {
    pthread_mutex_lock(&PAGE_FILE_MUTEX);

    // check that current page file is valid
    if(!VALID_PAGE_FILE) {
        pthread_mutex_unlock(&PAGE_FILE_MUTEX);
        return -1;
    }

    // check that there is an entry to read
    if(NUM_PAGE_FILE_ENTRIES == 0) {
        VALID_PAGE_FILE = false;
        pthread_mutex_unlock(&PAGE_FILE_MUTEX);
        return -1;
    }

    SerializedPageDataHeader page_data_header;
    
    // read header
    memcpy(&page_data_header, CURRENT_PAGE_FILE_LOCATION, sizeof(SerializedPageDataHeader));
    CURRENT_PAGE_FILE_LOCATION += sizeof(SerializedPageDataHeader);
    pd.distance_from_seedlist = page_data_header.distance_from_seedlist;

    // read url and word vectors
    pd.url = deserialize_string(&CURRENT_PAGE_FILE_LOCATION);
    pd.words = deserialize_string_vector(&CURRENT_PAGE_FILE_LOCATION);
    pd.titlewords = deserialize_string_vector(&CURRENT_PAGE_FILE_LOCATION);

    NUM_PAGE_FILE_ENTRIES--;
    
    pthread_mutex_unlock(&PAGE_FILE_MUTEX);
    return 0;
}

// writes a string to buffer and increments buffer past the end of the serialized string
void serialize_string(void** buffer, const std::string& s){
    u_int16_t size = (s.size() < __UINT16_MAX__) ? s.size() : __UINT16_MAX__;  // strings longer than the uin16 max are automatically truncated

    // write size of string
    memcpy(*buffer, &size, sizeof(u_int16_t));
    buffer += sizeof(u_int16_t);

    // write contents of string
    memcpy(*buffer, s.data(), size);
    buffer += size;
}

// reads serialized string from buffer and increments buffer past the end of the string
std::string deserialize_string(void** buffer) {
    std::string s;
    u_int16_t size; // strings longer than the uin16 max are automatically truncated to save space on disk

    // resize string to serialized value
    memcpy(&size, *buffer, sizeof(u_int16_t));
    buffer += sizeof(u_int16_t);
    s.resize(size);

    // read contents of string
    memcpy(s.data(), *buffer, size);
    buffer += size;

    return std::move(s);
}

// writes a vector of strings to buffer and increments buffer past the end of the vector
void serialize_string_vector(void** buffer, const std::vector<std::string>& v) {
    u_int64_t size = v.size();

    // write size of vector
    memcpy(*buffer, &size, sizeof(u_int64_t));
    buffer += sizeof(u_int64_t);

    // write strings
    for(u_int64_t i = 0; i < size; i++) {
        serialize_string(buffer, v[i]);
    }
}

// reads a vector of strings from buffer and increments buffer past the end of the vector
std::vector<std::string> deserialize_string_vector(void** buffer) {
    std::vector<std::string> v;
    u_int64_t size;

    // resize vector to serialized value
    memcpy(&size, *buffer, sizeof(u_int64_t));
    buffer += sizeof(u_int64_t);
    v.resize(size);

    // read strings
    for(u_int64_t i = 0; i < size; i++) {
        v.push_back(deserialize_string(buffer));
    }

    return std::move(v);
}