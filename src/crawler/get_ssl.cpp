#include "frontier.h"
#include "get_ssl.h"
#include "robots.txt/RobotsTxt.h"
#include "robots.txt/RobotsCache.h"
#include "initializer.h"
#include "worker_thread.h"

#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <iostream>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cctype>
#include <signal.h>
#include <pthread.h>

get_ssl_return crawl_page(const std::string& input_url, std::string& page) {
    // Check blacklist
    if (is_in_blacklist(input_url)) return failure; // already in blacklist
    
    // Check robots.txt cache
    float waitTime = 0;
    crawl_status crawlStatus;
    do {
        crawlStatus = robotsCache.request_permission_to_crawl(input_url, waitTime);
        if (crawlStatus == wait_to_crawl) std::this_thread::sleep_for(std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double, std::ratio<1>>(waitTime)));
    } while (crawlStatus == wait_to_crawl && should_continue_running());

    if (crawlStatus == do_not_crawl) return failure;

    std::string current_url = input_url;
    int numRedirects = 0;

    std::string redirectUrl;
    get_ssl_return getSslStatus;
    do {
        getSslStatus = get_ssl(current_url, page, redirectUrl);
    } while (getSslStatus == again);

    if (getSslStatus == blacklist) return blacklist;
    if (getSslStatus == failure) return failure;
    
    // Loop to handle redirects
    while (getSslStatus == redirect) {
        numRedirects++;
        current_url = redirectUrl;
        getSslStatus = get_ssl(current_url, page, redirectUrl);

        if (numRedirects > 5) return blacklist;
    }

    return success;
}

// take a URL and return the HTML
get_ssl_return get_ssl(const std::string& input_url, std::string& page, std::string& redirectUrl){

    ParsedUrlSsl url(input_url.c_str());

    // Get the host address.
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* serverInfo = nullptr;

    // Determine the port to use (default to 80 if not specified)
    const char* portToUse;
    if (url.Port[0] == '\0') {  // Check if port string is empty
        portToUse = "443";
    } else {
        portToUse = url.Port;
    }

    int status = getaddrinfo(url.Host, portToUse, &hints, &serverInfo);

    if (status != 0) {
        if (status == EAI_AGAIN) {
            pthread_mutex_lock(&dns_mutex);
            while(dns_wait) {
                pthread_cond_wait(&dns_cv, &dns_mutex);
            }
            dns_wait = true;
            pthread_mutex_unlock(&dns_mutex);
            std::this_thread::sleep_for(std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double, std::ratio<1>>(2)));  // avoid spamming
            pthread_mutex_lock(&dns_mutex);
            dns_wait = false;
            pthread_cond_signal(&dns_cv);
            pthread_mutex_unlock(&dns_mutex);
            std::cerr << "Error: " << gai_strerror(status) << std::endl;
            freeaddrinfo(serverInfo);
            return again;
        } else {
        std::cerr << "Error: " << gai_strerror(status) << std::endl;
        if(status == EAI_SYSTEM) {
            cout << "System Error: " << strerror(status) << "\n";
        }
        freeaddrinfo(serverInfo);
        return failure;
        }
    }

    // Create a TCP/IP socket.
    int socketFD = socket(serverInfo->ai_family, 
                            serverInfo->ai_socktype, 
                            serverInfo->ai_protocol);

    // Error Handling
    if (socketFD == -1) {
        std::cerr << "Failed to create socket" << std:: endl;
        freeaddrinfo(serverInfo);
        return failure;
    }

    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;

    setsockopt(socketFD, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(socketFD, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);

    // Connect the socket to the host address.
    int connectResult = connect(socketFD, serverInfo->ai_addr, serverInfo->ai_addrlen);

    // Error Handling
    if (connectResult == -1) {
        std::cerr << "Error connecting to server: " << strerror(errno) << std::endl;
        close(socketFD);
        freeaddrinfo(serverInfo);
        return failure;
    }

    // Build an SSL layer and set it to read/write
    // to the socket we've connected.
    SSL_library_init();

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    SSL_CTX* ctx = SSL_CTX_new(SSLv23_method());

    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, socketFD);

    SSL_set_tlsext_host_name(ssl, url.Host);

    SSL_connect(ssl);

    // Send a GET message.
    std::string request;

    // Make sure the path is correct (ALWAYS starts with /)
    std::string path;
    if (url.Path[0] == '\0') {
        path = "/";  // Empty path -> root
    } else if (url.Path[0] != '/') {
        // Path doesn't start with slash (shouldn't happen, but just in case)
        path = "/" + std::string(url.Path);
    } else {
        path = url.Path;  // Path already has leading slash
    }

    // First line: GET method, path, HTTP version
    request += "GET ";
    request += path;
    request += " HTTP/1.1\r\n";

    // Host Header
    request += "Host: ";
    request += url.Host;    // The hostname from the parsed URL
    request += "\r\n";

    // User-Agent - My info
    request += "User-Agent: " + USER_AGENT + "\r\n";

    // Accept Header
    request += "Accept: */*\r\n";    // Accept anything

    // Accept-Encoding
    request += "Accept-Encoding: identity\r\n";

    // Connection Header - close after response
    request += "Connection: close\r\n";

    // Extra blank line to end headers
    request += "\r\n";

    // Now we send it
    // using ssl this time!
    int bytesSent = SSL_write(ssl, request.c_str(), request.length());

    // Error Handling
    if (bytesSent == -1) {
        std::cerr << "Error sending request: " << strerror(errno) << std::endl;
        close(socketFD);
        freeaddrinfo(serverInfo);
        return failure;
    }

    // Read from the socket until there's no more data, copying it to
    // stdout.
    char buffer[8192];
    std::string headerBuf;
    while (headerBuf.find("\r\n\r\n") ==  std::string::npos){
        int bytes = SSL_read(ssl, buffer, sizeof(buffer));
        if (bytes <= 0){break;}
        headerBuf.append(buffer, bytes);
    }

    size_t headerEnd = headerBuf.find("\r\n\r\n");

    // redirect logic
    if (extract_redirect_url(headerBuf, std::string(url.Host), redirectUrl)) {
        // Cleanup connections before returning early
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(socketFD);
        SSL_CTX_free(ctx);
        freeaddrinfo(serverInfo);

        return redirect;
    }

    std::string outputData = "";

    if (headerEnd != std::string::npos) {

        // Write the content from this first read 
        size_t contentStart = headerEnd + 4;
        if (contentStart < headerBuf.size()) {
            //write(STDOUT_FILENO, headerBuf.data() + contentStart, headerBuf.size() - contentStart);
            outputData.append(headerBuf.data() + contentStart, headerBuf.size() - contentStart);
        }

        // Continue reading and writing remaining content
        int bytesRead;
        while ((bytesRead = SSL_read(ssl, buffer, sizeof(buffer))) > 0) {
            //write(STDOUT_FILENO, buffer, bytesRead);
            outputData.append(buffer, bytesRead);
        }

        // Close the socket and free the address info structure.
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(socketFD);
        SSL_CTX_free(ctx);
    }

    page = outputData;
    return success;
}

// Used to get the root domain (authority) for a given URL (useful for link distribution)
std::string extract_authority(const std::string& url){
    size_t start_pos = 0;

    // skip the scheme
    size_t scheme_pos = url.find("://");
    if (scheme_pos != std::string::npos) {
        start_pos = scheme_pos + 3;
    }

    // find the end of the host/domain block 
    size_t end_pos = url.find_first_of("/?#", start_pos);
    
    // extract everything between the scheme and the path
    std::string authority = url.substr(start_pos, end_pos - start_pos);

    // remove user authentication if present
    size_t auth_pos = authority.find('@');
    if (auth_pos != std::string::npos) {
        authority = authority.substr(auth_pos + 1);
    }

    // remove the port number if present
    size_t port_pos = authority.find(':');
    if (port_pos != std::string::npos) {
        authority = authority.substr(0, port_pos);
    }

    return authority;
}

std::string upgrade_to_https(const std::string &url) {
    if (url.find("https://") == 0) {
        return url;
    }
    
    if (url.find("http://") == 0) {
        return "https://" + url.substr(7); // 7 is the length of "http://"
    }
    
    return "https://" + url;
}

// Helper function to extract and format a redirect URL from HTTP headers
bool extract_redirect_url(const std::string& headerBuf, const std::string& host, std::string& redirectUrl) {
    
    // extract HTTP Status Code from the first line
    int statusCode = 0;
    size_t firstSpace = headerBuf.find(' ');
    if (firstSpace != std::string::npos) {
        size_t secondSpace = headerBuf.find(' ', firstSpace + 1);
        if (secondSpace != std::string::npos) {
            std::string codeStr = headerBuf.substr(firstSpace + 1, secondSpace - firstSpace - 1);
            try { statusCode = std::stoi(codeStr); } catch (...) {}
        }
    }

    // check if it's a 3xx Redirect Code
    if (statusCode >= 300 && statusCode < 400) {
       
        // search for location header (case-insensitive search)
        std::string lowerHeaders = headerBuf;
        std::transform(lowerHeaders.begin(), lowerHeaders.end(), lowerHeaders.begin(), ::tolower);
        
        size_t locPos = lowerHeaders.find("\r\nlocation: ");
        if (locPos != std::string::npos) {
            locPos += 12; // Length of "\r\nlocation: "
            size_t endLoc = headerBuf.find("\r\n", locPos);
            
            if (endLoc != std::string::npos) {
                redirectUrl = headerBuf.substr(locPos, endLoc - locPos);
                
                // trim leading/trailing whitespace
                redirectUrl.erase(0, redirectUrl.find_first_not_of(" \t"));
                redirectUrl.erase(redirectUrl.find_last_not_of(" \t") + 1);

                // handle relative redirects
                if (!redirectUrl.empty() && redirectUrl[0] == '/') {
                    redirectUrl = "https://" + host + redirectUrl;
                } else if (redirectUrl.find("http") != 0) {
                    redirectUrl = "https://" + host + "/" + redirectUrl;
                }
                
                return true;
            }
        }
    }
    
    return false;
}