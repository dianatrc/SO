/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef __SO_CONSUMER_H__
#define __SO_CONSUMER_H__

#include "ring_buffer.h"
#include "packet.h"

typedef struct {
	char buffer[256];
	unsigned long timestamp;
} log_entry;

typedef struct {
	log_entry *heap;
	int heap_capacity;
	int heap_size;
	int number_consumers;
} min_heap;

typedef struct so_consumer_ctx_t {
	struct so_ring_buffer_t *producer_rb;

    /* TODO: add synchronization primitives for timestamp ordering */
	pthread_mutex_t *log_mutex;  // shared mutex among all threads
	int *log_fd;  // pointer to output file descriptor
	min_heap *log_heap;
} so_consumer_ctx_t;

int create_consumers(pthread_t *tids,
					int num_consumers,
					so_ring_buffer_t *rb,
					const char *out_filename);

#endif /* __SO_CONSUMER_H__ */
