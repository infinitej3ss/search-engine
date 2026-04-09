#include "worker_thread.h"

bool STOP_CRAWLING = false;
pthread_mutex_t STOP_CRAWLING_MUTEX = PTHREAD_MUTEX_INITIALIZER;

bool should_continue_running() {
    bool should_stop;
    pthread_mutex_lock(&STOP_CRAWLING_MUTEX);
    should_stop = STOP_CRAWLING;
    pthread_mutex_unlock(&STOP_CRAWLING_MUTEX);
    return should_stop;
}

void run_worker_thread() {
    while (should_continue_running()) {
        // get url from frontier
        FrontierUrl frontier_url = get_url();

        // get html data from url
        std::string page_html;
        get_ssl_return ssl_status = get_ssl(frontier_url.url, page_html);

        if (ssl_status == failure) {
            continue;
        }
        if (ssl_status == blacklist) {
            // disable killing 
            blacklist_url(frontier_url.url);
            // enable killing
            continue;
        }

        // parse html
        HtmlParser parsed_html(page_html.c_str(), page_html.size());

        PageData page_data;
        page_data.distance_from_seedlist = frontier_url.distance_from_seedlist;
        page_data.url = frontier_url.url;
        page_data.titlewords = std::move(parsed_html.titleWords);
        page_data.words = std::move(parsed_html.words);

        // decide if this page is worth storing
        u_int64_t rank = 0; // TODO: decide rank group this page belongs to

        // distribute links
        // disable killing

        int new_dist_from_seedlist = frontier_url.distance_from_seedlist + 1;
        for(auto &link : parsed_html.links) {
            // NOTE: do we pass anchor text to the other machines too?
            
            URL_destination URL_dest = get_URL_destination(link.URL);
            FrontierUrl frontier_url = {new_dist_from_seedlist, link.URL, link.anchorText};

            if (URL_dest == frontier){ // if URL is for frontier
                insert_url(frontier_url);
            } else {                   // if URL is for a remote host
                int destHost = URL_to_destination(link.URL);
                send_URL_to_remote_host(link.URL, new_dist_from_seedlist, destHost); // NOTE: should take in a FrontierUrl struct
            }
        }

        // store data
        int write_status = write_page(rank, page_data);
        if(write_status == 1) {
            write_page_file(rank);
            if(rank == 0) {
                write_frontier_filters();
            }
        }
        // NOTE: probably store anchor text in hash tables, saving to disk when too large -> pass over tables after crawling and save as page files

        // enable killing
    }
}