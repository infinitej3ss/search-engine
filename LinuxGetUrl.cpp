#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <iostream>

class ParsedUrl
   {
   public:
      const char *CompleteUrl;
      char *Service, *Host, *Port, *Path;

      ParsedUrl( const char *url )
         {
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

int main( int argc, char **argv )
   {

   if ( argc != 2 )
      {
      std::cerr << "Usage: " << argv[ 0 ] << " url" << std::endl;
      return 1;
      }

   //-----------------------------------------
   //             Parse the URL
   //-----------------------------------------

   ParsedUrl url( argv[ 1 ] );

   // Get the host address.
   struct addrinfo hints;
   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_protocol = IPPROTO_TCP;

   struct addrinfo* serverInfo = nullptr;

   // Determine the port to use (default to 80 if not specified)
   const char* portToUse = (url.Port == url.Path) ? "80" : url.Port;

   int status = getaddrinfo(url.Host, portToUse, &hints, &serverInfo);

   if (status != 0) {
      std::cerr << "Error: " << gai_strerror(status) << std::endl;
      return 1;
   }

   //std::cout << "Successfully resolved " << url.Host << " to an IP address" << std::endl;

   //-----------------------------------------
   //       Create a TCP/IP socket.
   //-----------------------------------------

   int socketFD = socket(serverInfo->ai_family, 
                        serverInfo->ai_socktype, 
                        serverInfo->ai_protocol);

   // Error Handling
   if (socketFD == -1) {
      std::cerr << "Failed to create socket" << std:: endl;
      freeaddrinfo(serverInfo);
      return 1;
   }

   
   //-----------------------------------------
   // Connect the socket to the host address.
   //-----------------------------------------

   int connectResult = connect(socketFD, serverInfo->ai_addr, serverInfo->ai_addrlen);

   // Error Handling
   if (connectResult == -1) {
      std::cerr << "Error connecting to server: " << strerror(errno) << std::endl;
      close(socketFD);
      freeaddrinfo(serverInfo);
      return 1;
   }


   //-----------------------------------------
   //          Send a GET message
   //-----------------------------------------

   
   std::string request;

   // Make sure the path exists (default to "/" if empty)
   const char* path = url.Path;
   if (path == nullptr || path[0] == '\0') {
      path = "/";
   }

   // First line: GET method, path, HTTP version
   request += "GET ";
   request += path;    // The path from parsed URL
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
   int bytesSent = send(socketFD, request.c_str(), request.length(), 0);

   // Error Handling
   if (bytesSent == -1) {
      std::cerr << "Error sending request: " << strerror(errno) << std::endl;
      close(socketFD);
      freeaddrinfo(serverInfo);
      return 1;
   }

   
   // -----------------------------------------
   // Read from the socket until there's no 
   //       more data, copying it to
   //                stdout.
   // -----------------------------------------


   char buffer[8192];
   ssize_t bytesRead = recv(socketFD, buffer, sizeof(buffer), 0);

   if (bytesRead <= 0) {
      std::cerr << "Error receiving data: " << strerror(errno) << std::endl;
      return 1;
   }

   // Find the blank line
   std::string response(buffer, bytesRead);
   size_t headerEnd = response.find("\r\n\r\n");

   if (headerEnd != std::string::npos) {
      // Write the content from this first read 
      size_t contentStart = headerEnd + 4;
      if (contentStart < bytesRead) {
         write(STDOUT_FILENO, buffer + contentStart, bytesRead - contentStart);
      }

      // Continue reading and writing remaining content
      while ((bytesRead = recv(socketFD, buffer, sizeof(buffer), 0)) > 0) {
         write(STDOUT_FILENO, buffer, bytesRead);
      }
   }

   // Close the socket and free the address info structure.
   close(socketFD);
   freeaddrinfo(serverInfo);

   }
