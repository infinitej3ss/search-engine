// Object that manages robots.txt data for the crawler
// Provides a helpful interface for request status + crawl delay
#pragma once


#include "RobotsTxt.h"
#include "../get_ssl.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

using namespace std;

enum fetch_status {
    NOT_FETCHED, // robots file is not in the cache
    FETCHING,    // request for robots file has begun, hasn't been recieved
    FETCHED,     // robots file is in the cache
    NON_EXISTENT // robots file was requested, but was not present (crawl your heart out)
};

enum crawl_status {
    can_crawl,
    wait_to_crawl,
    do_not_crawl
};

struct RobotsCacheEntry {
    shared_ptr<RobotsTxt> robotsFile;
    fetch_status status;
    std::chrono::steady_clock::time_point last_crawled;
};

inline string url_to_origin(const string &url) {
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

class RobotsCache {
    
    private:
    unordered_map<string, RobotsCacheEntry> cache; // keyed by origin
    mutex cacheMutex;

    void put_cache_entry(const string &origin, RobotsCacheEntry entry){
        lock_guard<mutex> lock(cacheMutex);
        cache[origin] = move(entry);
    }

    void put_cache_status(const string &origin, const fetch_status &status){
        lock_guard<mutex> lock(cacheMutex);
        auto it = cache.find(origin);
        if (it != cache.end()) {
            it->second.status = status;
            it->second.last_crawled = std::chrono::steady_clock::now();
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
    bool request_robots_file(const string& url) {
        // getssl request for robots.txt
        string pageData;
        string robotsURL = url + "/robots.txt";
        string redirect;
        get_ssl_return returnStatus = get_ssl(robotsURL, pageData, redirect);


        // update cache
        if (returnStatus != success) {
            put_cache_status(url, NON_EXISTENT);
            return false;
        } else {
            auto now = std::chrono::steady_clock::now();
            put_cache_entry(url, RobotsCacheEntry { make_shared<RobotsTxt>((uint8_t*)pageData.data(), pageData.length()),
                                                        FETCHED,
                                                        now });

            return true;
        }
    }

    // check crawlability of a URL
    crawl_status request_permission_to_crawl(const string &url, float &timeLeftToCrawl) {
        string origin = url_to_origin(url);
        
        unique_lock<mutex> lock(cacheMutex); // lock for reading/writing the timestamp
        
        bool flag = false;
        auto it = cache.find(origin);
        if (it == cache.end() || it->second.status == FETCHING) {
            // if there is no entry, fetch the robots.txt
            if (it == cache.end()) {
                string origin = url_to_origin(url);  // convert URL to origin (scheme + domain + port)

                // set status to FETCHING
                cache[origin] = RobotsCacheEntry{nullptr, FETCHING};
                lock.unlock();
                request_robots_file(origin);
                lock.lock();
                it = cache.find(origin);
                flag = true;
                timeLeftToCrawl = 0;
            } else {
                timeLeftToCrawl = 1;
                return wait_to_crawl;
            }
        }

        int crawlDelay = 0;
        RobotsCacheEntry entry = it->second;
        lock.unlock();
        if (entry.status != NON_EXISTENT) {
            // check if the URL itself is actually allowed
            // The UrlAllowed function optionally reports the crawl delay back to us.
            bool allowed = entry.robotsFile->UrlAllowed(
                (const Utf8*)USER_AGENT.c_str(),
                (const Utf8*)url.c_str(),
                &crawlDelay);
            if (!allowed) {
                return do_not_crawl;
            }
        }

        if (crawlDelay < 1) {
            crawlDelay = 1;
        }

        lock.lock();
        it = cache.find(origin);
        // check if enough time has passed since the last crawl
        auto now = std::chrono::steady_clock::now();
        auto seconds_since_last_crawl = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_crawled).count();

        if (seconds_since_last_crawl >= crawlDelay || flag) {
            // We are cleared to crawl! Update the timestamp for the next thread.
            it->second.last_crawled = now;
            return can_crawl;
        } else {
            // We are allowed to crawl this URL, but the origin is on cooldown.
            return wait_to_crawl;
        }
    }
};
