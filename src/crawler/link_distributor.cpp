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
#include <iostream>

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

void send_URL_to_remote_peer(FrontierUrl &url, int remoteID){

    // lock mutex
    unique_lock<mutex> lock(url_buffer_mutex);

    peers[remoteID].url_send_buffer.push_back(url);

    if (peers[remoteID].url_send_buffer.size() > URL_BATCH_SIZE) {
        lock.unlock();
        send_remote_peer_URL_vector(remoteID);
        std::cerr << "------- URL batch send initiated " << remoteID << "-------\n";
        return;
    }
}

// send a specified machine its list of links and clear it
int send_remote_peer_URL_vector(int remoteID) {
    unique_lock<mutex> lock(url_buffer_mutex);  // request lock

    // make sure vector hasn't been cleared while waiting for mutex
    if (peers[remoteID].url_send_buffer.size() <= URL_BATCH_SIZE) {
        return 0;
    }

    std::cerr << "--- SENDING DATA TO " << remoteID << " -------\n";
    std::vector<FrontierUrl> url_send_buffer = peers[remoteID].url_send_buffer;
    peers[remoteID].url_send_buffer.clear();

    // define server address
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(peers[remoteID].port);
    inet_pton(AF_INET, peers[remoteID].ip_address.c_str(), &server_address.sin_addr);
    lock.unlock();

    u_int64_t dataSize = serialized_frontier_url_vector_size(url_send_buffer);

    // allocate for the serialized data
    void* originalBuffer = malloc(dataSize + sizeof(u_int64_t));
    if (originalBuffer == nullptr) return -1; 

    // create a secondary pointer for the serialization function to move
    void* movingPointer = (u_int8_t*)originalBuffer + sizeof(u_int64_t);
    memcpy(originalBuffer, &dataSize, sizeof(u_int64_t));

    // serialize file data into buffer
    serialize_frontier_url_vector(&movingPointer, url_send_buffer);

    // create TCP socket for the sender
    int my_socket = socket(AF_INET, SOCK_STREAM, 0);

    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;

    setsockopt(my_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(my_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);

    // connect to server address
    int connectionStatus = connect(my_socket, (struct sockaddr*)&server_address, sizeof(server_address));
    if (connectionStatus < 0) {
        free(originalBuffer);
        close(my_socket);
        return -1;
    }

    // send exact bytes to machine endpoint using the original pointer
    send(my_socket, originalBuffer, dataSize + sizeof(u_int64_t), 0);

    // signal that we are done writing to prevent TCP deadlock
    shutdown(my_socket, SHUT_WR);

    // recv for "OK"
    char ackBuffer[64];
    memset(ackBuffer, 0, sizeof(ackBuffer));
    ssize_t recv_status = recv(my_socket, ackBuffer, sizeof(ackBuffer), 0);
    if(recv_status == -1) {
        std::cerr << "------recv failed-----\n";
    }

    // clean up memory and socket
    free(originalBuffer);
    close(my_socket);
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

        struct timeval timeout;
        timeout.tv_sec = 30;
        timeout.tv_usec = 0;

        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);

        std::cerr << "Client connection accepted!\n";

        if (client_fd < 0) continue;

        std::vector<char> incomingData;
        char buffer[4096]; // declare buffer for reading in chunks
        u_int64_t data_size;
        int bytesReceived = recv(client_fd, buffer, sizeof(u_int64_t), MSG_WAITALL);
        if(bytesReceived != sizeof(u_int64_t)) {
            std::cerr << "client invalid packet\n";
            close(client_fd);
            continue;
        }
        memcpy(&data_size, buffer, sizeof(u_int64_t));

        u_int64_t total_bytes = 0;
        // read in bytes on a loop
        while (true) {
            int bytesReceived = recv(client_fd, buffer, sizeof(buffer), 0);
            
            if (bytesReceived > 0 && bytesReceived <= 4096) {
                incomingData.insert(incomingData.end(), buffer, buffer + bytesReceived);
                total_bytes += bytesReceived;
            } else {
                break; // 0 means done (shutdown received), -1 means error
            }
        }

        if(total_bytes != incomingData.size()) {
            std::cerr << "total bytes received and incomingData size mismatch\n";
            close(client_fd);
            continue;
        }

        if(total_bytes != data_size) {
            std::cerr << "total bytes received and data_size mismatch\n";
            close(client_fd);
            continue;
        }

        std::cerr << incomingData.size() << " bytes recieved\n";

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

        cerr << "Server ACK sent to client\n";

        // close TCP connection
        close(client_fd);
    }
    
    return nullptr;
}