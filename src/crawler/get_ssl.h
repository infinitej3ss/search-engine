#pragma once

#include "robots.txt/RobotsTxt.h"
#include "string.h"
#include <chrono>
#include <memory>

enum get_ssl_return {
    failure,
    blacklist,
    success
};

enum robots_cache_status {
    exists,
    does_not_exist,
    not_yet_fetched
};

struct RobotsCacheEntry {
    std::unique_ptr<RobotsTxt> robotsObject;
    // when to fetch the next page from this domain? (useful for crawl delay)
    std::chrono::steady_clock::time_point nextAllowedPageFetch; 
};

class ParsedUrl {
   public:
      const char *CompleteUrl;
      char *Service, *Host, *Port, *Path;

      ParsedUrl( const char *url ) {
         // Assumes url points to static text but
         // does not check.

         CompleteUrl = url;

         pathBuffer = new char[ strlen( url ) + 1 ];
         const char *f;
         char *t;
         for ( t = pathBuffer, f = url; *t++ = *f++; )
            ;

         Service = pathBuffer;

         const char Colon = ':', Slash = '/';
         char *p;
         for ( p = pathBuffer; *p && *p != Colon; p++ )
            ;

         if ( *p )
            {
            // Mark the end of the Service.
            *p++ = 0;

            if ( *p == Slash )
               p++;
            if ( *p == Slash )
               p++;

            Host = p;

            for ( ; *p && *p != Slash && *p != Colon; p++ )
               ;

            if ( *p == Colon )
               {
               // Port specified.  Skip over the colon and
               // the port number.
               *p++ = 0;
               Port = +p;
               for ( ; *p && *p != Slash; p++ )
                  ;
               }
            else
               Port = p;

            if ( *p )
               // Mark the end of the Host and Port.
               *p++ = 0;

            // Whatever remains is the Path.
            Path = p;
            }
         else
            Host = Path = p;
         }

      ~ParsedUrl( )
         {
         delete[ ] pathBuffer;
         }

    private:
        char *pathBuffer;
};

// take a URL and return the HTML
get_ssl_return get_ssl(std::string& url, std::string& page);

// used to get the root domain for a given URL (useful for robots.txt cache)
std::string extract_domain(std::string& url);

// returns whether a given domain's robots.txt has been fetched, has not, or doesn't exist
robots_cache_status get_robots_cache_status(std::string& domain);

// attempts to get the robots.txt file for a given page
// true = found a file | false = did not
bool get_robots_file(std::string& domain);

// will create an entry in robotsCache associated with the supplied domain and file
void add_robots_to_cache(std::string& robots_file, std::string& domain);