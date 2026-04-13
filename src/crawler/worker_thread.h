#pragma once

#include <pthread.h>
#include <string>

bool should_continue_running();

void stop_crawling();

void *run_worker_thread(void *in);