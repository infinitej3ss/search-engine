// Linux tiny HTTP server.
// Nicole Hamilton  nham@umich.edu

// This variation of LinuxTinyServer supports a simple plugin interface
// to allow "magic paths" to be intercepted.  (But the autograder will
// not test this feature.)

// Usage:  LinuxTinyServer port rootdirectory

// Compile with g++ -pthread LinuxTinyServer.cpp -o LinuxTinyServer
// To run under WSL (Windows Subsystem for Linux), you may have to
// elevate with sudo if the bind fails.

// LinuxTinyServer does not look for default index.htm or similar
// files.  If it receives a GET request on a directory, it will refuse
// it, returning an HTTP 403 error, access denied.  This could be
// improved.

// It also does not support HTTP Connection: keep-alive requests and
// will close the socket at the end of each response.  This is a
// perf issue, forcing the client browser to reconnect for each
// request and a candidate for improvement.


#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <iostream>
#include <string.h>
#include <string>
#include <cassert>

using namespace std;


 // The constructor for any plugin should set Plugin = this so that
 // LinuxTinyServer knows it exists and can call it.

#include "Plugin.h"
PluginObject *Plugin = nullptr;


// Root directory for the website, taken from argv[ 2 ].
// (Yes, a global variable since it never changes.)

char *RootDirectory;


//  Multipurpose Internet Mail Extensions (MIME) types

struct MimetypeMap
   {
   const char *Extension, *Mimetype;
   };

const MimetypeMap MimeTable[ ] =
   {
   // List of some of the most common MIME types in sorted order.
   // https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types/Complete_list_of_MIME_types
   ".3g2",     "video/3gpp2",
   ".3gp",     "video/3gpp",
   ".7z",      "application/x-7z-compressed",
   ".aac",     "audio/aac",
   ".abw",     "application/x-abiword",
   ".arc",     "application/octet-stream",
   ".avi",     "video/x-msvideo",
   ".azw",     "application/vnd.amazon.ebook",
   ".bin",     "application/octet-stream",
   ".bz",      "application/x-bzip",
   ".bz2",     "application/x-bzip2",
   ".csh",     "application/x-csh",
   ".css",     "text/css",
   ".csv",     "text/csv",
   ".doc",     "application/msword",
   ".docx",    "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
   ".eot",     "application/vnd.ms-fontobject",
   ".epub",    "application/epub+zip",
   ".gif",     "image/gif",
   ".htm",     "text/html",
   ".html",    "text/html",
   ".ico",     "image/x-icon",
   ".ics",     "text/calendar",
   ".jar",     "application/java-archive",
   ".jpeg",    "image/jpeg",
   ".jpg",     "image/jpeg",
   ".js",      "application/javascript",
   ".json",    "application/json",
   ".mid",     "audio/midi",
   ".midi",    "audio/midi",
   ".mpeg",    "video/mpeg",
   ".mpkg",    "application/vnd.apple.installer+xml",
   ".odp",     "application/vnd.oasis.opendocument.presentation",
   ".ods",     "application/vnd.oasis.opendocument.spreadsheet",
   ".odt",     "application/vnd.oasis.opendocument.text",
   ".oga",     "audio/ogg",
   ".ogv",     "video/ogg",
   ".ogx",     "application/ogg",
   ".otf",     "font/otf",
   ".pdf",     "application/pdf",
   ".png",     "image/png",
   ".ppt",     "application/vnd.ms-powerpoint",
   ".pptx",    "application/vnd.openxmlformats-officedocument.presentationml.presentation",
   ".rar",     "application/x-rar-compressed",
   ".rtf",     "application/rtf",
   ".sh",      "application/x-sh",
   ".svg",     "image/svg+xml",
   ".swf",     "application/x-shockwave-flash",
   ".tar",     "application/x-tar",
   ".tif",     "image/tiff",
   ".tiff",    "image/tiff",
   ".ts",      "application/typescript",
   ".ttf",     "font/ttf",
   ".vsd",     "application/vnd.visio",
   ".wav",     "audio/x-wav",
   ".weba",    "audio/webm",
   ".webm",    "video/webm",
   ".webp",    "image/webp",
   ".woff",    "font/woff",
   ".woff2",   "font/woff2",
   ".xhtml",   "application/xhtml+xml",
   ".xls",     "application/vnd.ms-excel",
   ".xlsx",    "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
   ".xml",     "application/xml",
   ".xul",     "application/vnd.mozilla.xul+xml",
   ".zip",     "application/zip"
   };


const char *Mimetype( const string filename )
   {
   // Return the Mimetype associated with any extension on the filename.

   //    YOUR CODE HERE
    // find(".") and then get the extension content
    // return the value according to the key in the dict
    size_t lastDotPos = filename.find_last_of('.');
    if (lastDotPos != string::npos && lastDotPos < filename.length() - 1) {
        // Extract the substring starting from the character after the dot to the end
        string ext = filename.substr(lastDotPos);
        
        for (auto &mimetype_map : MimeTable) {
            if (ext == mimetype_map.Extension) {
                return mimetype_map.Mimetype;
            }
        }
    }

   // Anything not matched is an "octet-stream", treated as
   // an unknown binary, which browsers treat as a download.

   return "application/octet-stream";
   }


int HexLiteralCharacter( char c )
   {
   // If c contains the Ascii code for a hex character, return the
   // binary value; otherwise, -1.

   int i;

   if ( '0' <= c && c <= '9' )
      i = c - '0';
   else
      if ( 'a' <= c && c <= 'f' )
         i = c - 'a' + 10;
      else
         if ( 'A' <= c && c <= 'F' )
            i = c - 'A' + 10;
         else
            i = -1;

   return i;
   }


string UnencodeUrlEncoding( string &path )
   {
   // Unencode any %xx encodings of characters that can't be
   // passed in a URL.

   // (Unencoding can only shorten a string or leave it unchanged.
   // It never gets longer.)

   const char *start = path.c_str( ), *from = start;
   string result;
   char c, d;


   while ( ( c = *from++ ) != 0 )
      if ( c == '%' )
         {
         c = *from;
         if ( c )
            {
            d = *++from;
            if ( d )
               {
               int i, j;
               i = HexLiteralCharacter( c );
               j = HexLiteralCharacter( d );
               if ( i >= 0 && j >= 0 )
                  {
                  from++;
                  result += ( char )( i << 4 | j );
                  }
               else
                  {
                  // If the two characters following the %
                  // aren't both hex digits, treat as
                  // literal text.

                  result += '%';
                  from--;
                  }
               }
            }
         }
      else
         result += c;

   return result;
   }


bool SafePath( const char *path )
   {
   // Watch out for paths containing .. segments that
   // attempt to go higher than the root directory
   // for the website.

   // The path must start with a / .

   if ( *path != '/' )
      return false;

   // Return false for any path containing .. segments that
   // attempt to go higher than the root directory for the
   // website.

   //    YOUR CODE HERE
//    "/../../etc/passwd"     // return false
//    "/../secret"            // return false
//    "/data/../../../etc"    // return false
//    "/files/.."             // return false (end of the string)
//    "/file..txt"            // return true, in the middle of a file name
//    "/test/my..file.html"   // return true
//    "/data/file.."          // return true
//    "/..hidden"             // return true, this is a hidden file name
    
    
    const char *ptr = path;
    // check if the char is not the ending char
    while (*ptr) {
        if (ptr[0] == '.' && ptr[1] == '.') {
            if ((ptr == path || ptr[-1] == '/') &&
                (ptr[2] == '/' || ptr[2] == '\0')) {
                return false;
            }
        }
        ptr++;
    }

   return true;
   }


off_t FileSize( int f )
   {
   // Return -1 for directories.

   struct stat fileInfo;
   fstat( f, &fileInfo );
   if ( ( fileInfo.st_mode & S_IFMT ) == S_IFDIR )
      return -1;
   return fileInfo.st_size;
   }


void AccessDenied( int talkSocket )
   {
   const char accessDenied[ ] = "HTTP/1.1 403 Access Denied\r\n"
         "Content-Length: 0\r\n"
         "Connection: close\r\n\r\n";

   cout << accessDenied;
   send( talkSocket, accessDenied, sizeof( accessDenied ) - 1, 0 );
   }


void FileNotFound( int talkSocket )
   {
   const char fileNotFound[ ] = "HTTP/1.1 404 Not Found\r\n"
         "Content-Length: 0\r\n"
         "Connection: close\r\n\r\n";

   cout << fileNotFound;
   send( talkSocket, fileNotFound, sizeof( fileNotFound ) - 1, 0 );
   }


void *Talk( void *talkSocket )
{
    // 1. Cast from void * to int * to recover the talk socket id
    // then delete the copy passed on the heap.
    int talkFD = *((int*)talkSocket);
    delete (int*)talkSocket;

    // 2. Read the request from the socket and parse it to extract
    // the action and the path, unencoding any %xx encodings.
    char buffer[1024];
    std::string request;
    int bytesRead;

    // receive data until there is nothing left
    while ((bytesRead = recv(talkFD, buffer, sizeof(buffer), 0)) > 0){
        request.append(buffer, bytesRead);
        // break early if the packet header termination string is reached
        if (request.find("\r\n\r\n") != string::npos) break;
    }

    // Safety check: if we got an empty request, just close and bail out
    if (request.empty()) {
        close(talkFD);
        return nullptr;
    }

    // get the request type
    size_t endOfRequestType = request.find(" ");
    size_t endOfRequestPath = request.find(" ", endOfRequestType + 1);

    // Safety check: malformed request without spaces
    if (endOfRequestType == string::npos || endOfRequestPath == string::npos) {
        close(talkFD);
        return nullptr;
    }

    string reqType = request.substr(0, endOfRequestType);
    string reqPath = request.substr(endOfRequestType + 1, endOfRequestPath - endOfRequestType - 1);

    // unencode
    reqPath = UnencodeUrlEncoding(reqPath);
    string fullPath = RootDirectory + reqPath;

    // 3 & 4. Check for plugin and if it intercepts this path
    if (Plugin != nullptr && Plugin->MagicPath(reqPath)){
        string pluginResponse = Plugin->ProcessRequest(reqPath);
        string header = "HTTP/1.1 200 OK\r\nContent-Length: " + to_string(pluginResponse.length()) + "\r\nConnection: close\r\n\r\n";
        
        send(talkFD, header.c_str(), header.length(), 0);
        send(talkFD, pluginResponse.c_str(), pluginResponse.length(), 0);
    } 

    // 5. If it isn't intercepted, action must be "GET" and the path must be safe.
    // (Passed reqPath instead of fullPath to SafePath)
    else if (reqType == "GET" && SafePath(reqPath.c_str())){
        
        int openResult = open(fullPath.c_str(), O_RDONLY);
        if (openResult == -1){
            // check for non-existent file
            FileNotFound(talkFD);
        } else {
            off_t size = FileSize(openResult);
            if (size == -1){
                // 6. If the path refers to a directory, access denied.
                AccessDenied(talkFD);
                close(openResult);
            } else {
                // 7. If the path refers to a file, write it to the socket.
                string mimeType = Mimetype(reqPath);
                string header = "HTTP/1.1 200 OK\r\nContent-Type: " + mimeType + "\r\nContent-Length: " + to_string(size) + "\r\nConnection: close\r\n\r\n";
                
                // Send the HTTP header first
                send(talkFD, header.c_str(), header.length(), 0);

                // Read the file in chunks and send it to the socket
                char fileBuf[1024];
                ssize_t bytesReadFromFile;
                while ((bytesReadFromFile = read(openResult, fileBuf, sizeof(fileBuf))) > 0) {
                    send(talkFD, fileBuf, bytesReadFromFile, 0);
                }
                
                close(openResult);
            }
        }
    } else {
        // Fallback for unsafe paths or non-GET requests
        AccessDenied(talkFD);
    }

    // 8. Close the socket and return nullptr.
    close(talkFD);
    return nullptr;
}



int main( int argc, char **argv )
   {
   if ( argc != 3 )
      {
      cerr << "Usage:  " << argv[ 0 ] << " port rootdirectory" << endl;
      return 1;
      }

   int port = atoi( argv[ 1 ] );
   RootDirectory = argv[ 2 ];

   // Discard any trailing slash.  (Any path specified in
   // an HTTP header will have to start with /.)

   char *r = RootDirectory;
   if ( *r )
      {
      do
         r++;
      while ( *r );
      r--;
      if ( *r == '/' )
         *r = 0;
      }

   // We'll use two sockets, one for listening for new
   // connection requests, the other for talking to each
   // new client.

   int listenSocket, talkSocket;

   // Create socket address structures to go with each
   // socket.

   struct sockaddr_in listenAddress,  talkAddress;
   socklen_t talkAddressLength = sizeof( talkAddress );
   memset( &listenAddress, 0, sizeof( listenAddress ) );
   memset( &talkAddress, 0, sizeof( talkAddress ) );

   // Fill in details of where we'll listen.

   // We'll use the standard internet family of protocols.
   listenAddress.sin_family = AF_INET;

   // htons( ) transforms the port number from host (our)
   // byte-ordering into network byte-ordering (which could
   // be different).
   listenAddress.sin_port = htons( port );

   // INADDR_ANY means we'll accept connections to any IP
   // assigned to this machine.
   listenAddress.sin_addr.s_addr = htonl( INADDR_ANY );

   // Create the listenSocket, specifying that we'll r/w
   // it as a stream of bytes using TCP/IP.
   
   listenSocket = socket(AF_INET, SOCK_STREAM, 0);

   // Bind the listen socket to the IP address and protocol
   // where we'd like to listen for connections.


   if(bind(listenSocket, (sockaddr*)&listenAddress, sizeof(listenAddress))) {
      cerr << "failed listen bind\n";
      return 1;
   }

   // Begin listening for clients to connect to us.


   if(listen(listenSocket, SOMAXCONN)) {
      cerr << "failed listen\n";
      return 1;
   }


   // The second argument to listen( ) specifies the maximum
   // number of connection requests that can be allowed to
   // stack up waiting for us to accept them before Linux
   // starts refusing or ignoring new ones.
   //
   // SOMAXCONN is a system-configured default maximum socket
   // queue length.  (Under WSL Ubuntu, it's defined as 128
   // in /usr/include/x86_64-linux-gnu/bits/socket.h.)

   // Accept each new connection and create a thread to talk with
   // the client over the new talk socket that's created by Linux
   // when we accept the connection.


   while ( ( talkAddressLength = sizeof( talkAddress ), talkSocket = accept(listenSocket, (sockaddr*)&talkAddress, &talkAddressLength) ) && talkSocket != -1 )
      {
      // When creating a child thread, you get to pass a void *,
      // usually used as a pointer to an object with whatever
      // information the child needs.

      // The talk socket is passed on the heap rather than with a
      // pointer to the local variable because we're going to quickly
      // overwrite that local variable with the next accept( ).  Since
      // this is multithreaded, we can't predict whether the child will
      // run before we do that.  The child will be responsible for
      // freeing the resource.  We do not wait for the child thread
      // to complete.
      //
      // (A simpler alternative in this particular case would be to
      // caste the int talksocket to a void *, knowing that a void *
      // must be at least as large as the int.  But that would not
      // demonstrate what to do in the general case.)

      pthread_t child;
      pthread_create( &child, nullptr, Talk, new int (talkSocket) );
      pthread_detach( child );
      }

   close( listenSocket );
   }
