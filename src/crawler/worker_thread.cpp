#include "worker_thread.h"

#include "HtmlParser.h"
#include "frontier.h"
#include "get_ssl.h"
#include "link_distributor.h"
#include "page_data.h"
#include "../ranker/static/static_ranker.hpp"

bool STOP_CRAWLING = false;
pthread_mutex_t STOP_CRAWLING_MUTEX = PTHREAD_MUTEX_INITIALIZER;

bool should_continue_running() {
    bool should_stop;
    pthread_mutex_lock(&STOP_CRAWLING_MUTEX);
    should_stop = STOP_CRAWLING;
    pthread_mutex_unlock(&STOP_CRAWLING_MUTEX);
    return !should_stop;
}

void* run_worker_thread(void* in) {
    while (should_continue_running()) {
        // get url from frontier
        FrontierUrl frontier_url = get_url();

        // get html data from url
        std::string page_html;
        get_ssl_return ssl_status = crawl_page(frontier_url.url, page_html);

        if (ssl_status == failure) {
            continue;
        }
        if (ssl_status == blacklist) {
            blacklist_url(frontier_url.url);
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
        RankerInput rank_input;
        rank_input.hop_distance = frontier_url.distance_from_seedlist;
        rank_input.url = frontier_url.url;
        StaticRanker url_rank(rank_input);
        if(url_rank.rank() < 0) {
            continue;
        }
        u_int64_t rank = rank_bucket_from_double(url_rank.rank());

        // distribute links

        start_distribution_server();

        u_int32_t new_dist_from_seedlist = frontier_url.distance_from_seedlist + 1;
        for(auto &link : parsed_html.links) {
            URL_destination URL_dest = get_URL_destination(link.URL);
            FrontierUrl frontier_url = {new_dist_from_seedlist, link.URL, link.anchorText};

            if (URL_dest == frontier){ // if URL is for frontier
                insert_url(frontier_url);
            } else {                   // if URL is for a remote host
                int destHost = URL_to_destination(link.URL);
                send_URL_to_remote_peer(frontier_url, destHost);
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
    }

    return nullptr;
}

void stop_crawling() {
    pthread_mutex_lock(&STOP_CRAWLING_MUTEX);
    STOP_CRAWLING = true;
    pthread_mutex_unlock(&STOP_CRAWLING_MUTEX);
}