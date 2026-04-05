#include "frontier.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <queue>
#include <utility>

#include "page_data.h"

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

pthread_mutex_t FRONTIER_IO_MUTEX = PTHREAD_MUTEX_INITIALIZER;
std::string DIR_PATH = "./";

// inserts url into the frontier
int insert_url(FrontierUrl& url) {
    pthread_mutex_lock(&FRONTIER_IO_MUTEX);
    // check url against already seen and blacklisted domains bloom filters

    pthread_mutex_unlock(&FRONTIER_IO_MUTEX);

    // determine rank of url
    u_int64_t rank = 0;
    if (rank > NUM_FRONTIER_QUEUES) {
        return;
    }

    // place in corresponding staging vector
    pthread_mutex_lock(&STAGING_VECTORS[rank].mutex);

    STAGING_VECTORS[rank].data.push_back(std::move(url));

    // if vector is full, write to disk and add to file queue
    if (STAGING_VECTORS[rank].data.size() > NUM_URLS_PER_FILE) {
        // write to disk
        std::string file_name = DIR_PATH + "frontier_file_rank_" + std::to_string(rank) + "_num_" + std::to_string(STAGING_VECTORS[rank].num_written);

        u_int64_t file_size = sizeof(u_int64_t);
        for (auto& frontier_url : STAGING_VECTORS[rank].data) {
            file_size += sizeof(u_int32_t) + frontier_url.url.size();
        }

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
    // TODO: implement
    return;
}

// retreives url from the frontier
FrontierUrl get_url() {
    // TODO: implement
    FrontierUrl frontier_url;
    frontier_url.distance_from_seedlist = 0;
    frontier_url.url = "https://www.google.com";
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
    DIR_PATH = dir;

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