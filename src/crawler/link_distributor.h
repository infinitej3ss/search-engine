#include "string"

// given a URL, either pass it to the frontier or distribute to another machine
void process_new_url(std::string &url, u_int64_t distance_from_seedlist);

// send a file 
void send_url_file(int machineID);