#include "frontier.h"
#include "get_ssl.h"
#include "robots.txt/RobotsTxt.h"
#include "robots.txt/RobotsCache.h"
#include "initializer.h"

#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <iostream>
#include <unordered_map>

std::string USER_AGENT = "Jess Morton/1.0 (mortonjs@umich.edu)";

// Robots.txt cache
std::unordered_map<std::string, RobotsCacheEntry> robotsCache;

get_ssl_return crawl_page(const std::string& input_url, std::string& page){
    
    // Check blacklist
    if (is_in_blacklist(input_url)) return failure; // already in blacklist
    
    // Check robots.txt cache
    float waitTime = 0;
    crawl_status status = robotsCache.request_permission_to_crawl(input_url, &waitTime);
    if (status == do_not_crawl) return failure;

    // Proceed with fetching HTML

}

// take a URL and return the HTML
get_ssl_return get_ssl(const std::string& input_url, std::string& page){

    ParsedUrl url(input_url.c_str());

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
        std::cerr << "Error: " << gai_strerror(status) << std::endl;
        return failure;
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

    SSL_CTX *ctx = SSL_CTX_new( SSLv23_method());

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