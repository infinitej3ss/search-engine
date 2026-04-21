#include "page_data.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstring>
#include <utility>

// load given page file into memory for later use with get_next_page
int load_page_file(const std::string& file_name) {
    pthread_mutex_lock(&PAGE_FILE_MUTEX);

    // make sure any previous file is closed
    if (VALID_PAGE_FILE) {
        munmap(MAPPED_PAGE_FILE, MAPPED_PAGE_FILE_SIZE);
        VALID_PAGE_FILE = false;
    }

    // open the file
    int fd = open(file_name.c_str(), O_RDONLY);
    if(fd == -1) {
        close(fd);
        pthread_mutex_unlock(&PAGE_FILE_MUTEX);
        return -1;
    }

    // extract header
    PageFileHeader file_header;
    if(read(fd, &file_header, sizeof(PageFileHeader)) != sizeof(PageFileHeader)) {
        close(fd);
        pthread_mutex_unlock(&PAGE_FILE_MUTEX);
        return -1;
    }

    // check magic number to make sure this is a page file
    if(file_header.magic_number != CORRECT_MAGIC_NUMBER) {
        close(fd);
        pthread_mutex_unlock(&PAGE_FILE_MUTEX);
        return -1;
    }
    NUM_PAGE_FILE_ENTRIES = file_header.num_pages;
    PAGE_FILE_SIZE_BYTES = file_header.size_bytes;

    struct stat statbuf;
    fstat(fd, &statbuf);
    MAPPED_PAGE_FILE_SIZE = statbuf.st_size;

    // create memory mapping
    MAPPED_PAGE_FILE = mmap(NULL, MAPPED_PAGE_FILE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    CURRENT_PAGE_FILE_LOCATION = (u_int8_t*)MAPPED_PAGE_FILE + sizeof(PageFileHeader);
    close(fd);
    if(MAPPED_PAGE_FILE == MAP_FAILED) {
        pthread_mutex_unlock(&PAGE_FILE_MUTEX);
        return -1;
    }
    VALID_PAGE_FILE = true;
    PAGE_FILE_CORRUPT = false;
    pthread_mutex_unlock(&PAGE_FILE_MUTEX);
    return 0;
}

// closes the current page file if loaded
void close_page_file() {
    pthread_mutex_lock(&PAGE_FILE_MUTEX);
    if (VALID_PAGE_FILE) {
        munmap(MAPPED_PAGE_FILE, MAPPED_PAGE_FILE_SIZE);
        VALID_PAGE_FILE = false;
    }
    pthread_mutex_unlock(&PAGE_FILE_MUTEX);
}

// loads the next page's data
int get_next_page(PageData& pd) {
    pthread_mutex_lock(&PAGE_FILE_MUTEX);
    int index_of_page = (char*)CURRENT_PAGE_FILE_LOCATION - (char*)MAPPED_PAGE_FILE;

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
    CURRENT_PAGE_FILE_LOCATION = (u_int8_t*)CURRENT_PAGE_FILE_LOCATION + sizeof(SerializedPageDataHeader);
    pd.distance_from_seedlist = page_data_header.distance_from_seedlist;

    // read url and word vectors
    pd.url = deserialize_string(&CURRENT_PAGE_FILE_LOCATION);
    pd.words = deserialize_string_vector(&CURRENT_PAGE_FILE_LOCATION);
    pd.titlewords = deserialize_string_vector(&CURRENT_PAGE_FILE_LOCATION);
    pd.anchor_text = deserialize_string_vector(&CURRENT_PAGE_FILE_LOCATION);

    if (PAGE_FILE_CORRUPT) {
        VALID_PAGE_FILE = false;
        pthread_mutex_unlock(&PAGE_FILE_MUTEX);
        return -1;
    }

    NUM_PAGE_FILE_ENTRIES--;

    pthread_mutex_unlock(&PAGE_FILE_MUTEX);
    return index_of_page;
}

// write page data to specified rank file
int write_page(u_int64_t rank_file, PageData& pd) {
    // check for valid rank_file
    if (rank_file >= NUM_PAGE_FILE_RANKS) {
        return -1;
    }

    pthread_mutex_lock(&NUM_CRAWLED_MUTEX);
    NUM_CRAWLED_PAGES++;
    pthread_mutex_unlock(&NUM_CRAWLED_MUTEX);

    u_int64_t data_size = sizeof(SerializedPageDataHeader) + 3 * sizeof(u_int64_t);
    data_size += sizeof(u_int16_t) + pd.url.size();
    for(auto &word : pd.titlewords) {
        data_size += sizeof(u_int16_t) + word.size();
    }
    for (auto& word : pd.words) {
        data_size += sizeof(u_int16_t) + word.size();
    }
    for (auto& word : pd.anchor_text) {
        data_size += sizeof(u_int16_t) + word.size();
    }

    std::vector<u_int8_t> serialized_data = std::vector<u_int8_t>(data_size);  // initialize with data_size bytes
    void* current_location = serialized_data.data();

    // write header
    memcpy(current_location, &pd.distance_from_seedlist, sizeof(u_int64_t));
    current_location = (u_int8_t*)current_location + sizeof(u_int64_t);

    // write data
    serialize_string(&current_location, pd.url);
    serialize_string_vector(&current_location, pd.words);
    serialize_string_vector(&current_location, pd.titlewords);
    serialize_string_vector(&current_location, pd.anchor_text);

    // resize data vector
    data_size = (u_int8_t*)current_location - serialized_data.data();
    if (data_size != serialized_data.size()) {
        serialized_data.resize(data_size);
    }

    pthread_mutex_lock(&PAGE_FILES[rank_file].page_file_mutex);

    // add to page file
    PAGE_FILES[rank_file].num_pages++;
    PAGE_FILES[rank_file].size_bytes += data_size;
    PAGE_FILES[rank_file].page_data_entries.push_back(serialized_data);

    // check if page file is now full
    if (PAGE_FILES[rank_file].size_bytes > MAX_PAGE_FILE_SIZE_BYTES) {
        pthread_mutex_unlock(&PAGE_FILES[rank_file].page_file_mutex);
        return 1;
    }

    pthread_mutex_unlock(&PAGE_FILES[rank_file].page_file_mutex);
    return 0;
}

// write specified page file to disk
int write_page_file(u_int64_t rank_file) {
    // check for valid rank_file
    if (rank_file >= NUM_PAGE_FILE_RANKS) {
        return -1;
    }

    pthread_mutex_lock(&PAGE_FILES[rank_file].page_file_mutex);

    if (PAGE_FILES[rank_file].size_bytes <= MAX_PAGE_FILE_SIZE_BYTES) {
        pthread_mutex_unlock(&PAGE_FILES[rank_file].page_file_mutex);
        return 1;
    }

    std::string file_name = DIR_PATH + "crawled_page_data_rank_" + std::to_string(rank_file) + "_num_" + std::to_string(PAGE_FILES[rank_file].num_files_of_this_rank_written);
    PAGE_FILES[rank_file].num_files_of_this_rank_written++;

    // open file
    int fd = open(file_name.c_str(), O_WRONLY | O_CREAT, S_IRWXU);
    if (fd == -1) {
        close(fd);
        pthread_mutex_unlock(&PAGE_FILES[rank_file].page_file_mutex);
        return -1;
    }

    // write header
    PageFileHeader header;
    header.magic_number = CORRECT_MAGIC_NUMBER;
    header.size_bytes = PAGE_FILES[rank_file].size_bytes;
    header.num_pages = PAGE_FILES[rank_file].num_pages;

    if (write(fd, &header, sizeof(PageFileHeader)) != sizeof(PageFileHeader)) {
        close(fd);
        pthread_mutex_unlock(&PAGE_FILES[rank_file].page_file_mutex);
        return -1;
    }

    // write page data
    for (auto &pd : PAGE_FILES[rank_file].page_data_entries) {
        if (write(fd, pd.data(), pd.size()) != pd.size()) {
            close(fd);
            pthread_mutex_unlock(&PAGE_FILES[rank_file].page_file_mutex);
            return -1;
        }
    }

    close(fd);

    // reset file in memory
    PAGE_FILES[rank_file].num_pages = 0;
    PAGE_FILES[rank_file].size_bytes = 0;
    PAGE_FILES[rank_file].page_data_entries = std::vector<std::vector<u_int8_t>>();

    pthread_mutex_unlock(&PAGE_FILES[rank_file].page_file_mutex);

    return 0;
}

// writes a string to buffer and increments buffer past the end of the serialized string
void serialize_string(void** buffer, const std::string& s){
    u_int16_t size = (s.size() < __UINT16_MAX__) ? s.size() : __UINT16_MAX__;  // strings longer than the uin16 max are automatically truncated

    // write size of string
    memcpy(*buffer, &size, sizeof(u_int16_t));
    *buffer = (u_int8_t*)*buffer + sizeof(u_int16_t);

    // write contents of string
    memcpy(*buffer, s.data(), size);
    *buffer = (u_int8_t*)*buffer + size;
}

// returns true if [buffer, buffer + n) lies entirely within the mmap
// region. used to bail on corrupt size prefixes before memcpy reads
// past the mapped range and segfaults
static bool in_bounds(const void* buffer, u_int64_t n) {
    if (!MAPPED_PAGE_FILE || MAPPED_PAGE_FILE_SIZE == 0) return false;
    const u_int8_t* start = (const u_int8_t*)MAPPED_PAGE_FILE;
    const u_int8_t* end = start + MAPPED_PAGE_FILE_SIZE;
    const u_int8_t* p = (const u_int8_t*)buffer;
    return p >= start && p <= end && n <= (u_int64_t)(end - p);
}

// reads serialized string from buffer and increments buffer past the end of the string
std::string deserialize_string(void** buffer) {
    std::string s;
    u_int16_t size; // strings longer than the uin16 max are automatically truncated to save space on disk

    if (!in_bounds(*buffer, sizeof(u_int16_t))) {
        PAGE_FILE_CORRUPT = true;
        return s;
    }

    // resize string to serialized value
    memcpy(&size, *buffer, sizeof(u_int16_t));
    *buffer = (u_int8_t*)*buffer + sizeof(u_int16_t);

    if (!in_bounds(*buffer, size)) {
        PAGE_FILE_CORRUPT = true;
        return s;
    }

    s.resize(size);

    // read contents of string
    memcpy(s.data(), *buffer, size);
    *buffer = (u_int8_t*)*buffer + size;

    return std::move(s);
}

// writes a vector of strings to buffer and increments buffer past the end of the vector
void serialize_string_vector(void** buffer, const std::vector<std::string>& v) {
    u_int64_t size = v.size();

    // write size of vector
    memcpy(*buffer, &size, sizeof(u_int64_t));
    *buffer = (u_int8_t*)*buffer + sizeof(u_int64_t);

    // write strings
    for (auto& s : v) {
        serialize_string(buffer, s);
    }
}

// reads a vector of strings from buffer and increments buffer past the end of the vector
std::vector<std::string> deserialize_string_vector(void** buffer) {
    std::vector<std::string> v;
    u_int64_t size;

    if (!in_bounds(*buffer, sizeof(u_int64_t))) {
        PAGE_FILE_CORRUPT = true;
        return v;
    }

    memcpy(&size, *buffer, sizeof(u_int64_t));
    *buffer = (u_int8_t*)*buffer + sizeof(u_int64_t);

    // a vector size larger than the remaining bytes cannot possibly
    // be legitimate (each element is at least sizeof(u_int16_t) bytes)
    u_int64_t remaining =
        (u_int8_t*)MAPPED_PAGE_FILE + MAPPED_PAGE_FILE_SIZE - (u_int8_t*)*buffer;
    if (size > remaining / sizeof(u_int16_t)) {
        PAGE_FILE_CORRUPT = true;
        return v;
    }

    v.resize(size);

    // read strings
    for(u_int64_t i = 0; i < size; i++) {
        v.at(i) = deserialize_string(buffer);
        if (PAGE_FILE_CORRUPT) { v.resize(i); break; }
    }

    return std::move(v);
}

// find rank of a given page file name
//
// returns __INT64_MAX__ if the page is invalid
u_int64_t get_page_file_rank(const std::string& file_name) {
    if (file_name.length() < std::string("crawled_page_data_rank_x").length()) {
        return __INT64_MAX__;
    }

    u_int64_t index = std::string("crawled_page_data_rank_").length();  // location the number should start at

    // search until finding the end of the rank number
    while (index < file_name.length() && file_name[index] != '_') {
        index++;
    }

    if (index == file_name.length() || file_name[index] != '_') {
        return __INT64_MAX__;
    }

    std::string rank(file_name.begin() + std::string("crawled_page_data_rank_").length(), file_name.begin() + index);

    return atoi(rank.c_str());
}

// find num of a given page file name
//
// returns __INT64_MAX__ if the page is invalid
u_int64_t get_page_file_num(const std::string& file_name) {
    const std::string marker = "_num_";
    size_t pos = file_name.rfind(marker);
    if (pos == std::string::npos) {
        return __INT64_MAX__;
    }
    return std::strtoull(file_name.c_str() + pos + marker.size(), nullptr, 10);
}

// initializes the page file directory specified for writing
int initialize_page_file_dir(const std::string& dir) {
    DIR* dir_ptr;
    struct dirent* ent;

    // open dir
    if ((dir_ptr = opendir(dir.c_str())) == NULL) {
        return -1;
    }
    DIR_PATH = dir;

    // iterate through files in dir
    while ((ent = readdir(dir_ptr)) != NULL) {
        std::string file_name(ent->d_name);

        u_int64_t rank = get_page_file_rank(file_name);
        if (rank == __INT64_MAX__) {  // invalid file
            continue;
        }

        if (rank >= NUM_PAGE_FILE_RANKS) {
            continue;
        }

        // increment files counter
        PAGE_FILES[rank].num_files_of_this_rank_written++;
    }

    return 0;
}

// get the names of page files in the specified directory
std::vector<std::vector<std::string>> get_page_file_names(const std::string& dir) {
    DIR* dir_ptr;
    struct dirent* ent;
    std::vector<std::vector<std::string>> v(NUM_PAGE_FILE_RANKS);

    // open dir
    if ((dir_ptr = opendir(dir.c_str())) == NULL) {
        return std::move(v);
    }

    // iterate through files in dir
    while ((ent = readdir(dir_ptr)) != NULL) {
        std::string file_name(ent->d_name);

        u_int64_t rank = get_page_file_rank(file_name);
        if (rank == __INT64_MAX__) {  // invalid file
            continue;
        }

        if (rank >= NUM_PAGE_FILE_RANKS) {
            continue;
        }

        v[rank].push_back(dir + file_name);
    }

    return std::move(v);
}

u_int64_t get_num_crawled_pages() {
    pthread_mutex_lock(&NUM_CRAWLED_MUTEX);
    u_int64_t return_value = NUM_CRAWLED_PAGES;
    pthread_mutex_unlock(&NUM_CRAWLED_MUTEX);
    return return_value;
}

// loads page data for a specific page using an index
int get_page_data_from_index(PageData& pd, const std::string& dir, const u_int64_t rank, const u_int64_t num, const u_int64_t index){
    std::string file_name = dir + "crawled_page_data_rank_" + std::to_string(rank) + "_num_" + std::to_string(num);
    pthread_mutex_lock(&PAGE_FILE_INDEXING_MUTEX);

    // load file
    if(load_page_file(file_name) == -1) {
        pthread_mutex_unlock(&PAGE_FILE_INDEXING_MUTEX);
        return -1;
    }

    // `index` is the offset returned by get_next_page, measured from the
    // start of the file (including the PageFileHeader). load_page_file has
    // already advanced CURRENT_PAGE_FILE_LOCATION past the header, so we set
    // it absolutely rather than add
    if (index >= MAPPED_PAGE_FILE_SIZE) {
        close_page_file();
        pthread_mutex_unlock(&PAGE_FILE_INDEXING_MUTEX);
        return -1;
    }
    CURRENT_PAGE_FILE_LOCATION = (u_int8_t*)MAPPED_PAGE_FILE + index;

    // load data
    //PageData pd;
    if (get_next_page(pd) == -1) {
        pthread_mutex_unlock(&PAGE_FILE_INDEXING_MUTEX);
        return -1;
    }

    // close
    close_page_file();

    pthread_mutex_unlock(&PAGE_FILE_INDEXING_MUTEX);
    return 0;
}

int scan_page_file_for_url(PageData& pd, const std::string& file_path, const std::string& target_url) {
    pthread_mutex_lock(&PAGE_FILE_INDEXING_MUTEX);
    if (load_page_file(file_path) == -1) {
        pthread_mutex_unlock(&PAGE_FILE_INDEXING_MUTEX);
        return -1;
    }
    PageData candidate;
    while (get_next_page(candidate) != -1) {
        if (candidate.url == target_url) {
            pd = std::move(candidate);
            close_page_file();
            pthread_mutex_unlock(&PAGE_FILE_INDEXING_MUTEX);
            return 0;
        }
    }
    close_page_file();
    pthread_mutex_unlock(&PAGE_FILE_INDEXING_MUTEX);
    return -1;
}

u_int64_t rank_bucket_from_double(const double score) {
    if(score > 1) {
        return 0;
    }
    if(score <= 0) {
        return NUM_PAGE_FILE_RANKS - 1;
    }
    return NUM_PAGE_FILE_RANKS - std::ceil(score * NUM_PAGE_FILE_RANKS);
}
