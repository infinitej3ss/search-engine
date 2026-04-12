// Object that manages robots.txt data for the crawler
// Provides a helpful interface for request status + crawl delay

#include "RobotsTxt.h"
#include "get_ssl.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

using namespace std;

enum fetch_status {
    NOT_FETCHED,
    FETCHING,
    FETCHED,
    NON_EXISTENT
};

struct RobotsCacheEntry {
    shared_ptr<RobotsTxt> robotsFile;
    fetch_status status;
};

class RobotsCache {
    
    private:
    unordered_map<string, RobotsCacheEntry> cache;
    mutex cacheMutex;

    public:
    RobotsCache() {}

    // File can either be unfetched, fetching, fetched, or non existent
    fetch_status check_robots_file_status(const string &authority) {
        lock_guard<mutex> lock(cacheMutex);

        auto it = cache.find(authority);
        if (it != cache.end()){
            return it->second.status;
        }
        
        return NOT_FETCHED; 
    }

    // Returns true if the request was successful
    bool request_robots_file(const string &authority) {

        // set status to FETCHING
        {
            lock_guard<mutex> lock(cacheMutex);
            cache[authority] = RobotsCacheEntry{nullptr, FETCHING};
        }

        // getssl request for robots.txt
        string pageData;
        string robotsURL = authority + "/robots.txt";
        get_ssl_return returnStatus = get_ssl(robotsURL, pageData);

        // update cache
        if (returnStatus != success) { 
            put_cache_status(authority, NON_EXISTENT);
            return false;
        } else {
            put_cache_entry(authority, RobotsCacheEntry{
                make_shared<RobotsTxt>((uint8_t*)pageData.data(), pageData.length()), 
                FETCHED
            });

            return true;
        }
    }

    void put_cache_entry(const string &authority, RobotsCacheEntry entry){
        lock_guard<mutex> lock(cacheMutex);
        cache[authority] = move(entry);
    }

    void put_cache_status(const string &authority, const fetch_status &status){
        lock_guard<mutex> lock(cacheMutex);
        auto it = cache.find(authority);
        if (it != cache.end()) {
            it->second.status = status;
        }
    }

    RobotsCacheEntry read_cache(const string &authority) {
        lock_guard<mutex> lock(cacheMutex);
        
        auto it = cache.find(authority);
        if (it != cache.end()) {
            return it->second; 
        }
        
        return RobotsCacheEntry{nullptr, NOT_FETCHED}; 
    }
};