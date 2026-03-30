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

void run_worker_thread();