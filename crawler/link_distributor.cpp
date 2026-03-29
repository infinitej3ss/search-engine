#include "string"

// given a URL, either pass it to the frontier or distribute to another machine
void process_new_url(std::string url){

    // hash url and take mod (MD5)

    // check if url is for the current machine

    // if not, append url to file for the target machine

    // if the file is large enough, send it
}

// send a specified machine its list of links and clear it
void send_url_file(int machineID){
    
    // serialize file data into buffer

    // open up TCP connection to machine

    // send file to machine

    // wait for ack from machine

    // clear file
}