#pragma once

#include "frontier.h"
#include "get_ssl.h"
#include "HtmlParser.h"
#include "page_data.h"
#include "link_distributor.h"

#include <pthread.h>
#include <string>

bool STOP_CRAWLING = false;
pthread_mutex_t STOP_CRAWLING_MUTEX = PTHREAD_MUTEX_INITIALIZER;

bool should_continue_running();

void stop_crawling() {
    pthread_mutex_lock(&STOP_CRAWLING_MUTEX);
    STOP_CRAWLING = true;
    pthread_mutex_unlock(&STOP_CRAWLING_MUTEX);
}

void *run_worker_thread(void *in);