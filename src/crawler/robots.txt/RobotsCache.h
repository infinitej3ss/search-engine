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
    unordered_map<string, RobotsCacheEntry> cache; // keyed by origin
    mutex cacheMutex;

    public:
    RobotsCache() {}

    // File can either be unfetched, fetching, fetched, or non existent
    fetch_status check_robots_file_status(const string &origin) {
        lock_guard<mutex> lock(cacheMutex);

        auto it = cache.find(origin);
        if (it != cache.end()){
            return it->second.status;
        }

        return NOT_FETCHED; 
    }

    // Returns true if the request was successful
    bool request_robots_file(const string &origin) {

        // set status to FETCHING
        {
            lock_guard<mutex> lock(cacheMutex);
            cache[origin] = RobotsCacheEntry{nullptr, FETCHING};
        }

        // getssl request for robots.txt
        string pageData;
        string robotsURL = origin + "/robots.txt";
        get_ssl_return returnStatus = get_ssl(robotsURL, pageData);

        // update cache
        if (returnStatus != success) { 
            put_cache_status(origin, NON_EXISTENT);
            return false;
        } else {
            put_cache_entry(origin, RobotsCacheEntry{
                make_shared<RobotsTxt>((uint8_t*)pageData.data(), pageData.length()), 
                FETCHED
            });

            return true;
        }
    }

    void put_cache_entry(const string &origin, RobotsCacheEntry entry){
        lock_guard<mutex> lock(cacheMutex);
        cache[origin] = move(entry);
    }

    void put_cache_status(const string &origin, const fetch_status &status){
        lock_guard<mutex> lock(cacheMutex);
        auto it = cache.find(origin);
        if (it != cache.end()) {
            it->second.status = status;
        }
    }

    RobotsCacheEntry read_cache(const string &origin) {
        lock_guard<mutex> lock(cacheMutex);
        
        auto it = cache.find(origin);
        if (it != cache.end()) {
            return it->second; 
        }
        
        return RobotsCacheEntry{nullptr, NOT_FETCHED}; 
    }
};

string domain_to_origin(const string &url) {
    // Find the end of the origin (the first slash after the scheme)
    // We start searching from index 8 to safely skip "https://" or "http://"
    size_t scheme_end = url.find("://");
    size_t start_search = (scheme_end != string::npos) ? scheme_end + 3 : 0;
    
    size_t path_start = url.find_first_of("/?#", start_search);

    if (path_start == string::npos) {
        return url; // No path, the whole URL is the origin
    } else {
        return url.substr(0, path_start); // Keep the scheme AND the origin
    }
}