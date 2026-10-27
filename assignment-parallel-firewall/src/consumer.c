// SPDX-License-Identifier: BSD-3-Clause

#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "consumer.h"
#include "ring_buffer.h"
#include "packet.h"
#include "utils.h"

#define HEAP_CAPACITY 100

// initialize global min heap
min_heap global_heap;

void swap_entries(log_entry *a, log_entry *b)
{
	log_entry temp = *a;
	*a = *b;
	*b = temp;
}

void heap_up(log_entry *heap, int idx)
{
	while (idx > 0) {
		int parent = (idx - 1) / 2;

		if (heap[idx].timestamp >= heap[parent].timestamp)
			break;
		swap_entries(&heap[idx], &heap[parent]);
		idx = parent;
	}
}

void sift_down(log_entry *heap, int size, int idx)
{
	while (1) {
		int smallest = idx;
		int left = 2 * idx + 1;
		int right = 2 * idx + 2;

		if (left < size && heap[left].timestamp < heap[smallest].timestamp)
			smallest = left;
		if (right < size && heap[right].timestamp < heap[smallest].timestamp)
			smallest = right;

		if (smallest == idx)
			break;

		swap_entries(&heap[idx], &heap[smallest]);
		idx = smallest;
	}
}

void heap_insert(min_heap *h, log_entry *entry)
{
	h->heap[h->heap_size] = *entry;
	heap_up(h->heap, h->heap_size);
	h->heap_size++;
}

log_entry heap_pop(min_heap *h)
{
	log_entry mini = h->heap[0];

	h->heap_size--;
	h->heap[0] = h->heap[h->heap_size];
	sift_down(h->heap, h->heap_size, 0);
	return mini;
}

void *consumer_thread(void *consumer)
{
	/* TODO: implement consumer thread */
	so_consumer_ctx_t *ctx = (so_consumer_ctx_t *)consumer;

	so_packet_t packet;
	so_action_t decision;
	unsigned long hash;
	unsigned long timestamp;
	log_entry entry;

	while (1) {
		// dequeue a packet
		if (ring_buffer_dequeue(ctx->producer_rb, &packet, sizeof(so_packet_t)) <= 0)
			break;

		decision = process_packet(&packet);
		hash = packet_hash(&packet);
		timestamp = packet.hdr.timestamp;

		// format output line, according to serial.c
		snprintf(entry.buffer, sizeof(entry.buffer), "%s %016lx %lu\n", RES_TO_STR(decision), hash, timestamp);
		entry.timestamp = timestamp;

		pthread_mutex_lock(ctx->log_mutex);

		heap_insert(ctx->log_heap, &entry);

		// when heap is full, write min
		if (ctx->log_heap->heap_size == ctx->log_heap->heap_capacity) {
			log_entry mini = heap_pop(ctx->log_heap);

			write(*ctx->log_fd, mini.buffer, strlen(mini.buffer));
		}

		pthread_mutex_unlock(ctx->log_mutex);
	}

	pthread_mutex_lock(ctx->log_mutex);
	ctx->log_heap->number_consumers--;

	// last thread writes everything that remains
	if (ctx->log_heap->number_consumers == 0)
		while (ctx->log_heap->heap_size > 0) {
			log_entry mini = heap_pop(ctx->log_heap);

			write(*ctx->log_fd, mini.buffer, strlen(mini.buffer));
		}
	pthread_mutex_unlock(ctx->log_mutex);
	return NULL;
}

int create_consumers(pthread_t *tids,
					 int num_consumers,
					 struct so_ring_buffer_t *rb,
					 const char *out_filename)
{
	pthread_mutex_t *log_mutex = malloc(sizeof(pthread_mutex_t));
	int *log_fd = malloc(sizeof(int));

	if (!log_mutex || !log_fd) {
		perror("malloc error");
		if (log_mutex)
			free(log_mutex);
		if (log_fd)
			free(log_fd);
		return -1;
	}

	pthread_mutex_init(log_mutex, NULL);

	// open output file
	*log_fd = open(out_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (*log_fd < 0) {
		perror("open error");
		pthread_mutex_destroy(log_mutex);
		free(log_mutex);
		free(log_fd);
		return -1;
	}

	so_consumer_ctx_t *contexts = malloc(num_consumers * sizeof(so_consumer_ctx_t));

	if (!contexts) {
		perror("malloc error");
		close(*log_fd);
		pthread_mutex_destroy(log_mutex);
		free(log_mutex);
		free(log_fd);
		return -1;
	}

	global_heap.heap = malloc(HEAP_CAPACITY * sizeof(log_entry));

	if (!global_heap.heap) {
		perror("malloc error");
		close(*log_fd);
		pthread_mutex_destroy(log_mutex);
		free(log_mutex);
		free(log_fd);
		free(contexts);
		return -1;
	}

	global_heap.heap_capacity = HEAP_CAPACITY;
	global_heap.heap_size = 0;
	global_heap.number_consumers = num_consumers;

	for (int i = 0; i < num_consumers; i++) {
		/*
		 * TODO: Launch consumer threads
		 **/
		contexts[i].producer_rb = rb;
		contexts[i].log_fd = log_fd;
		contexts[i].log_mutex = log_mutex;
		contexts[i].log_heap = &global_heap;

		if (pthread_create(&tids[i], NULL, consumer_thread, &contexts[i]) != 0) {
			perror("pthread create error");
			close(*log_fd);
			pthread_mutex_destroy(log_mutex);
			free(log_mutex);
			free(log_fd);
			free(contexts);
			return -1;
		}
	}

	return num_consumers;
}
