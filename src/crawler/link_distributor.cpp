#include "link_distributor.h"

#include <functional>  // Required for std::hash
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
//#include <cstdlib>

#include "get_ssl.h"
#include "initializer.h"
#include "frontier.h"

// gets whether a URL is for the frontier or a remote machine, writes remoteID
URL_destination get_URL_destination(std::string &url, int &remoteID){

    int numHosts = peers.size();
    if (numHosts == 0){
        remoteID = -1;
        return frontier;
    }

    // Use C++ built-in string hashing
    std::hash<std::string> hasher;
    size_t hash_val = hasher(extract_authority(url));
    remoteID = hash_val % numHosts;

    if (remoteID != machineID) return remote_host;

    return frontier;
}

// send a specified machine its list of links and clear it
int send_remote_peer_URL_vector(int remoteID) {
    
    // 1. Allocate a large enough buffer for the serialized data (e.g., 10 MB)
    // Adjust this size if your URL batches are expected to be larger!
    void* originalBuffer = malloc(10 * 1024 * 1024);
    if (originalBuffer == nullptr) return -1; 

    // 2. Create a secondary pointer for the serialization function to move
    void* movingPointer = originalBuffer;

    // serialize file data into buffer
    serialize_frontier_url_vector(&movingPointer, peers[remoteID].url_send_buffer);

    // 3. Calculate exact byte size using pointer math
    size_t dataSize = (uint8_t*)movingPointer - (uint8_t*)originalBuffer;

    // create TCP socket for the sender
    int my_socket = socket(AF_INET, SOCK_STREAM, 0);

    // define server address
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(peers[remoteID].port);
    inet_pton(AF_INET, peers[remoteID].ip_address.c_str(), &server_address.sin_addr);

    // connect to server address
    int connectionStatus = connect(my_socket, (struct sockaddr*)&server_address, sizeof(server_address));
    if (connectionStatus < 0) {
        free(originalBuffer);
        close(my_socket);
        return -1;
    }

    // send exact bytes to machine endpoint using the original pointer
    send(my_socket, originalBuffer, dataSize, 0);

    // signal that we are done writing to prevent TCP deadlock
    shutdown(my_socket, SHUT_WR);

    // recv for "OK"
    // TODO: timeout after blocking for a certain period of time
    char ackBuffer[64];
    memset(ackBuffer, 0, sizeof(ackBuffer));
    recv(my_socket, ackBuffer, sizeof(ackBuffer), 0);

    // clean up memory and socket
    free(originalBuffer);
    close(my_socket);

    // clear vector
    {
        std::lock_guard<std::mutex> lock(url_buffer_mutex);
        peers[remoteID].url_send_buffer.clear();
    }

    return 0; // success
}

void* start_distribution_server(void* arg) {
    
    int serverFD = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(peers[machineID].port);
    address.sin_addr.s_addr = INADDR_ANY;

    bind(serverFD, (struct sockaddr*)&address, sizeof(address));

    listen(serverFD, 10); // 10 is the backlog queue size

    // create loop to accept incoming TCP connections
    while (true) {

        // get a new file descriptor
        int client_fd = accept(serverFD, nullptr, nullptr);
        if (client_fd < 0) continue;

        std::vector<char> incomingData;
        char buffer[4096]; // declare buffer for reading in chunks

        // read in bytes on a loop
        while (true) {
            int bytesReceived = recv(client_fd, buffer, sizeof(buffer), 0);
            
            if (bytesReceived > 0) {
                incomingData.insert(incomingData.end(), buffer, buffer + bytesReceived);
            } else {
                break; // 0 means done (shutdown received), -1 means error
            }
        }

        if (!incomingData.empty()) {
            // point to the start of the received byte vector
            void* movingPointer = incomingData.data();

            // deserialize into vector of FrontierURLs
            std::vector<FrontierUrl> urlVector = deserialize_frontier_url_vector(&movingPointer);

            // insert list into frontier
            insert_url_vector(urlVector);
        }

        // send "OK"
        std::string ack = "OK";
        send(client_fd, ack.c_str(), ack.length(), 0);

        // close TCP connection
        close(client_fd);
    }
    
    return nullptr;
}