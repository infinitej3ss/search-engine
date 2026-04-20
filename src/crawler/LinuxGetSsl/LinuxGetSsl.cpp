#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <openssl/ssl.h>

#include <iostream>

int get_page_data_from_URL(std::string input_url, std::string &pageData);

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

int main( int argc, char **argv ) {

   if ( argc != 2 )
      {
      std::cerr << "Usage: " << argv[ 0 ] << " url" << std::endl;
      return 1;
      }

    // Parse the URL
    std::string input_url = argv[1];
    std::string pageData;
    
    // call getSSL function
    int exitCode = get_page_data_from_URL(input_url, pageData);
    if (exitCode == 0) std::cout << pageData;
    return exitCode;
}

int get_page_data_from_URL(std::string input_url, std::string &pageData){
    
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
        return 1;
    }

    // Create a TCP/IP socket.
    int socketFD = socket(serverInfo->ai_family, 
                            serverInfo->ai_socktype, 
                            serverInfo->ai_protocol);

    // Error Handling
    if (socketFD == -1) {
        std::cerr << "Failed to create socket" << std:: endl;
        freeaddrinfo(serverInfo);
        return 1;
    }

    // Connect the socket to the host address.
    int connectResult = connect(socketFD, serverInfo->ai_addr, serverInfo->ai_addrlen);

    // Error Handling
    if (connectResult == -1) {
        std::cerr << "Error connecting to server: " << strerror(errno) << std::endl;
        close(socketFD);
        freeaddrinfo(serverInfo);
        return 1;
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
    request += "User-Agent: YaireLopezqui/1.0 (lopezqui@umich.edu)\r\n";

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
        return 1;
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

    pageData = outputData;
    return 0;
}