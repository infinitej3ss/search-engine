#include "frontier.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <random>
#include <queue>
#include <utility>
#include <string_view>

#include "BloomFilter.h"
#include "page_data.h"
#include "get_ssl.h"

// stores a queue of frontier urls
struct FrontierQueue {
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    std::queue<FrontierUrl> data;
};

// stores a queue of file names containing frontier url vectors
struct FrontierStagingFileQueue {
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    std::queue<std::string> data;
};

// stores a vector of frontier urls to later be written to disk
struct FrontierStagingVector {
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    u_int64_t num_written = 0;
    std::vector<FrontierUrl> data;
};

const u_int64_t NUM_FRONTIER_QUEUES = 10;
const u_int64_t NUM_URLS_PER_FILE = 10000;

FrontierQueue FRONTIER_QUEUES[NUM_FRONTIER_QUEUES];
FrontierStagingFileQueue STAGING_FILE_QUEUES[NUM_FRONTIER_QUEUES];
FrontierStagingVector STAGING_VECTORS[NUM_FRONTIER_QUEUES];
Bloomfilter BLACKLIST(10000000, 0.05);
std::string BLACKLIST_TAG = "frontier_blacklist";
Bloomfilter SEEN(50000000, 0.05);
std::string SEEN_TAG = "frontier_seen";
std::random_device rd;
std::mt19937 gen(rd());
std::geometric_distribution<> random_gen;

pthread_mutex_t FRONTIER_IO_MUTEX = PTHREAD_MUTEX_INITIALIZER;
std::string FRONTIER_DIR_PATH = "./";

void write_frontier_filters() {
    pthread_mutex_lock(&FRONTIER_IO_MUTEX);
    BLACKLIST.write_data(BLACKLIST_TAG);
    SEEN.write_data(SEEN_TAG);
    pthread_mutex_unlock(&FRONTIER_IO_MUTEX);
}

void load_frontier_filters() {
    pthread_mutex_lock(&FRONTIER_IO_MUTEX);
    BLACKLIST.load_data(BLACKLIST_TAG);
    SEEN.load_data(SEEN_TAG);
    pthread_mutex_unlock(&FRONTIER_IO_MUTEX);
}

// tests url against blacklist by iterating though root paths
bool is_in_blacklist(std::string &url) {
    size_t pos = 7; // skip initial https://
    if(url.length() < 8) {
        return BLACKLIST.contains(url);
    }
    do {
        pos = url.find('/', pos + 1);
        if (BLACKLIST.contains(url.substr(0, (pos == std::string_view::npos) ? std::string_view::npos : pos))
            || BLACKLIST.contains(url.substr(0, (pos == std::string_view::npos) ? std::string_view::npos : pos + 1))) {
            return true;
        }
    } while(pos != std::string_view::npos);
    return false;
}

// inserts url into the frontier
int insert_url(FrontierUrl& url) {

    url.url = upgrade_to_https(url.url);

    pthread_mutex_lock(&FRONTIER_IO_MUTEX);
    // check url against already seen and blacklisted domains bloom filters
    if(SEEN.contains(url.url)) {
        pthread_mutex_unlock(&FRONTIER_IO_MUTEX);
        return -1;
    }

    if (is_in_blacklist(url.url)) {
        pthread_mutex_unlock(&FRONTIER_IO_MUTEX);
        return -1;
    }

    // add to seen filter
    SEEN.insert(url.url);

    pthread_mutex_unlock(&FRONTIER_IO_MUTEX);

    // determine rank of url
    u_int64_t rank = 0;
    if (rank > NUM_FRONTIER_QUEUES) {
        return -1;
    }

    // place in corresponding staging vector
    pthread_mutex_lock(&STAGING_VECTORS[rank].mutex);

    STAGING_VECTORS[rank].data.push_back(std::move(url));

    // if vector is full, write to disk and add to file queue
    if (STAGING_VECTORS[rank].data.size() > NUM_URLS_PER_FILE) {
        // write to disk
        std::string file_name = FRONTIER_DIR_PATH + "frontier_file_rank_" + std::to_string(rank) + "_num_" + std::to_string(STAGING_VECTORS[rank].num_written);

        u_int64_t file_size = serialized_frontier_url_vector_size(STAGING_VECTORS[rank].data);

        char* buffer = new char[file_size];
        serialize_frontier_url_vector((void**)&buffer, STAGING_VECTORS[rank].data);

        int fd = open(file_name.c_str(), O_WRONLY | O_CREAT, S_IRWXU);
        if (fd == -1) {
            close(fd);
            pthread_mutex_unlock(&STAGING_VECTORS[rank].mutex);
            return -1;
        }

        if (write(fd, buffer, file_size) != file_size) {
            close(fd);
            pthread_mutex_unlock(&STAGING_VECTORS[rank].mutex);
            return -1;
        }

        close(fd);
        delete[] buffer;

        // reset vector
        STAGING_VECTORS[rank].data = std::vector<FrontierUrl>();

        // add to file queue
        pthread_mutex_lock(&STAGING_FILE_QUEUES[rank].mutex);
        STAGING_FILE_QUEUES[rank].data.push(file_name);
        pthread_mutex_unlock(&STAGING_FILE_QUEUES[rank].mutex);
    }

    pthread_mutex_unlock(&STAGING_VECTORS[rank].mutex);
    return 0;
}

// insert vector of urls
int insert_url_vector(std::vector<FrontierUrl>& url_vector) {
    for (auto& url : url_vector) {
        if (insert_url(url) == -1) {
            return -1;
        }
    }
    return 0;
}

// adds url to blacklist, preventing it from being inserted or retreived from the frontier
void blacklist_url(std::string& url) {
    pthread_mutex_lock(&FRONTIER_IO_MUTEX);
    size_t special_char = url.find_first_of("&%#+;@");
    BLACKLIST.insert(url.substr(0, special_char));
    pthread_mutex_unlock(&FRONTIER_IO_MUTEX);
}

// retreives url from the frontier
FrontierUrl get_url() {
    // pick queue at random
    pthread_mutex_lock(&FRONTIER_IO_MUTEX);
    u_int64_t queue_num = random_gen(gen);
    if (queue_num >= NUM_FRONTIER_QUEUES) {
        queue_num = 0;
    }
    pthread_mutex_unlock(&FRONTIER_IO_MUTEX);

    // pull url from queue
    pthread_mutex_lock(&FRONTIER_QUEUES[queue_num].mutex);

    // load queue from file if empty
    if(FRONTIER_QUEUES[queue_num].data.empty()) {
        pthread_mutex_lock(&STAGING_FILE_QUEUES[queue_num].mutex);
        if(STAGING_FILE_QUEUES[queue_num].data.empty()) {
            // try another queue
            pthread_mutex_unlock(&STAGING_FILE_QUEUES[queue_num].mutex);
            pthread_mutex_unlock(&FRONTIER_QUEUES[queue_num].mutex);
            return get_url();
        }

        // load file
        std::string file_name = STAGING_FILE_QUEUES[queue_num].data.front();
        STAGING_FILE_QUEUES[queue_num].data.pop();
        pthread_mutex_unlock(&STAGING_FILE_QUEUES[queue_num].mutex);

        int fd = open(file_name.c_str(), O_WRONLY, S_IRWXU);
        if(fd == -1) {
            close(fd);
            // try another queue
            pthread_mutex_unlock(&FRONTIER_QUEUES[queue_num].mutex);
            return get_url();
        }

        struct stat statbuf;
        fstat(fd, &statbuf);
        u_int64_t file_size = statbuf.st_size;

        char* buf = new char[file_size];
        read(fd, buf, file_size);
        std::vector<FrontierUrl> urls = deserialize_frontier_url_vector((void**)&buf);
        delete[] buf;

        for(auto &u : urls) {
            FRONTIER_QUEUES[queue_num].data.push(u);
        }
        close(fd);

        // delete file
        remove(file_name.c_str());
    }

    FrontierUrl frontier_url = FRONTIER_QUEUES[queue_num].data.front();
    FRONTIER_QUEUES[queue_num].data.pop();

    pthread_mutex_unlock(&FRONTIER_QUEUES[queue_num].mutex);

    // check against blacklist
    pthread_mutex_lock(&FRONTIER_IO_MUTEX);
    if(is_in_blacklist(frontier_url.url)) {
        pthread_mutex_unlock(&FRONTIER_IO_MUTEX);
        return get_url();
    }
    pthread_mutex_unlock(&FRONTIER_IO_MUTEX);

    return std::move(frontier_url);
}

// writes a vector of frontier urls to buffer and increments buffer past the end of the vector
void serialize_frontier_url_vector(void** buffer, const std::vector<FrontierUrl>& v) {
    u_int64_t size = v.size();

    // write size of vector
    memcpy(*buffer, &size, sizeof(u_int64_t));
    *buffer += sizeof(u_int64_t);

    // write frontier urls
    for (auto& s : v) {
        memcpy(*buffer, &s.distance_from_seedlist, sizeof(u_int32_t));
        *buffer += sizeof(u_int32_t);
        serialize_string(buffer, s.url);
        serialize_string_vector(buffer, s.anchor_text);
    }
}

// reads a vector of frontier urls from buffer and increments buffer past the end of the vector
std::vector<FrontierUrl> deserialize_frontier_url_vector(void** buffer) {
    std::vector<FrontierUrl> v;
    u_int64_t size;

    // resize vector to serialized value
    memcpy(&size, *buffer, sizeof(u_int64_t));
    *buffer += sizeof(u_int64_t);
    v.resize(size);

    // read strings
    for (u_int64_t i = 0; i < size; i++) {
        memcpy(&v.at(i).distance_from_seedlist, *buffer, sizeof(u_int32_t));
        *buffer += sizeof(u_int32_t);
        v.at(i).url = deserialize_string(buffer);
        v.at(i).anchor_text = deserialize_string_vector(buffer);
    }

    return std::move(v);
}

struct frontier_file_data {
    u_int64_t rank = __INT64_MAX__;
    u_int64_t num = __INT64_MAX__;
};

// find rank of a given frontier file name
//
// returns __INT64_MAX__ if the file is invalid
frontier_file_data get_frontier_file_rank(std::string& file_name) {
    frontier_file_data return_value;
    if (file_name.length() < std::string("frontier_file_rank_x").length()) {
        return return_value;
    }

    u_int64_t index = std::string("frontier_file_rank_").length();  // location the number should start at

    // search until finding the end of the rank number
    while (index < file_name.length() && file_name[index] != '_') {
        index++;
    }

    if (index == file_name.length() || file_name[index] != '_') {
        return return_value;
    }

    std::string rank(file_name.begin() + std::string("frontier_file_rank_").length(), file_name.begin() + index);

    return_value.rank = atoi(rank.c_str());

    // get num of file
    index += std::string("_num_").length();
    std::string num(file_name.begin() + index, file_name.end());
    return_value.num = atoi(num.c_str());

    return return_value;
}

// initializes the frontier file directory
int initialize_frontier_file_dir(const std::string& dir) {
    DIR* dir_ptr;
    struct dirent* ent;

    // open dir
    if ((dir_ptr = opendir(dir.c_str())) == NULL) {
        return -1;
    }
    FRONTIER_DIR_PATH = dir;

    // iterate through files in dir
    while ((ent = readdir(dir_ptr)) != NULL) {
        std::string file_name(ent->d_name);

        frontier_file_data data = get_frontier_file_rank(file_name);
        if (data.rank == __INT64_MAX__) {  // invalid file
            continue;
        }

        if (data.rank >= NUM_FRONTIER_QUEUES) {
            continue;
        }

        if (data.num > STAGING_VECTORS[data.rank].num_written) {
            STAGING_VECTORS[data.rank].num_written = data.num + 1;
        }

        // add to file queue
        STAGING_FILE_QUEUES[data.rank].data.push(dir + file_name);
    }

    return 0;
}

// calculates size of a vector of frontier urls after serialization
u_int64_t serialized_frontier_url_vector_size(const std::vector<FrontierUrl>& v) {
    u_int64_t file_size = sizeof(u_int64_t);
    for (auto& frontier_url : v) {
        file_size += sizeof(u_int32_t) + frontier_url.url.size() + sizeof(u_int64_t);
        for(auto& word : frontier_url.anchor_text) {
            file_size += sizeof(u_int16_t) + word.size();
        }
    }
    return file_size;
}

void insert_seed_list(FrontierUrl& u) {
    SEEN.insert(u.url);
    FRONTIER_QUEUES[0].data.push(u);
}