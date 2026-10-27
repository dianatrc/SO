// SPDX-License-Identifier: BSD-3-Clause

#include "ring_buffer.h"
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

int ring_buffer_init(so_ring_buffer_t *ring, size_t cap)
{
	/* TODO: implement ring_buffer_init */
	ring->data = malloc(cap);
	if (!ring->data) {
		perror("malloc error");
		return -1;
	}
	ring->read_pos = 0;
	ring->write_pos = 0;
	ring->len = 0;
	ring->cap = cap;
	ring->stop = 0;

	if (pthread_mutex_init(&ring->mutex, NULL) != 0) {
		perror("mutex init error");
		free(ring->data);
		return -1;
	}

	if (pthread_cond_init(&ring->not_empty, NULL) != 0) {
		perror("cond init error");
		pthread_mutex_destroy(&ring->mutex);
		free(ring->data);
		return -1;
	}

	if (pthread_cond_init(&ring->not_full, NULL) != 0) {
		perror("cond init error");
		pthread_cond_destroy(&ring->not_empty);
		pthread_mutex_destroy(&ring->mutex);
		free(ring->data);
		return -1;
	}
	return 0;
}

ssize_t ring_buffer_enqueue(so_ring_buffer_t *ring, void *data, size_t size)
{
	/* TODO: implement ring_buffer_enqueue */
	pthread_mutex_lock(&ring->mutex);

	// wait while buffer is not full and stop = 0
	while (ring->len + size > ring->cap && !ring->stop)
		pthread_cond_wait(&ring->not_full, &ring->mutex);

	if (ring->stop != 0) {
		// printf("stopped\n");
		pthread_mutex_unlock(&ring->mutex);
		return -1;
	}

	const char *src = (const char *)data;
	size_t chunk = ring->cap - ring->write_pos;

	if (chunk >= size) {
		memcpy(ring->data + ring->write_pos, src, size);
		ring->write_pos = (ring->write_pos + size) % ring->cap;
	} else {
		memcpy(ring->data + ring->write_pos, src, chunk);
		memcpy(ring->data, src + chunk, size - chunk);
		ring->write_pos = size - chunk;
	}

	ring->len = ring->len + size; // update length

	pthread_cond_signal(&ring->not_empty); // signal data is available
	pthread_mutex_unlock(&ring->mutex);
	return size;
}

ssize_t ring_buffer_dequeue(so_ring_buffer_t *ring, void *data, size_t size)
{
	/* TODO: Implement ring_buffer_dequeue */
	(void) ring;
	(void) data;
	(void) size;

	pthread_mutex_lock(&ring->mutex);

	// wait while not enough data
	while (ring->len < size && !ring->stop)
		pthread_cond_wait(&ring->not_empty, &ring->mutex);

	if (ring->len < size) {
		pthread_mutex_unlock(&ring->mutex);
		return -1;
	}

	char *dest = (char *)data;
	size_t length, chunk = ring->cap - ring->read_pos;

	if (size < ring->len)
		length = size;
	else
		length = ring->len;

	if (chunk >= length) {
		memcpy(dest, ring->data + ring->read_pos, length);
		ring->read_pos = (ring->read_pos + length) % ring->cap;
	} else {
		memcpy(dest, ring->data + ring->read_pos, chunk);
		memcpy(dest + chunk, ring->data, length - chunk);
		ring->read_pos = length - chunk;
	}

	ring->len = ring->len - size;  // update length

	pthread_cond_signal(&ring->not_full);
	pthread_mutex_unlock(&ring->mutex);

	return size;
}

void ring_buffer_destroy(so_ring_buffer_t *ring)
{
	/* TODO: Implement ring_buffer_destroy */
	(void) ring;
	if (ring) {
		free(ring->data);
		pthread_mutex_destroy(&ring->mutex);
		pthread_cond_destroy(&ring->not_empty);
		pthread_cond_destroy(&ring->not_full);
	}
}

void ring_buffer_stop(so_ring_buffer_t *ring)
{
	/* TODO: Implement ring_buffer_stop */
	(void) ring;
	pthread_mutex_lock(&ring->mutex);
	ring->stop = 1;

	// wake up waiting threads
	pthread_cond_broadcast(&ring->not_empty);
	pthread_cond_broadcast(&ring->not_full);

	pthread_mutex_unlock(&ring->mutex);
}
