// SPDX-License-Identifier: BSD-3-Clause

#include <internal/mm/mem_list.h>
#include <internal/types.h>
#include <internal/essentials.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

void *malloc(size_t size)
{
	/* TODO: Implement malloc(). */
	if (size == 0) {
		size = 1;
	}
	size_t value = size;
	// round up if size is not a multiple of 16
	if (value % 16 != 0) {
		value = (value / 16 + 1) * 16;
	}
	// allocate requested memory
	void *p = mmap(NULL, value, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		return NULL;
	}
	mem_list_add(p, value);
	return p;
}

void *calloc(size_t nmemb, size_t size)
{
	/* TODO: Implement calloc(). */
	size_t bytes = nmemb * size;
	void *p = malloc(bytes);
	if (p == NULL)
		return NULL;
	memset(p, 0, bytes);
	return p;
}

void free(void *ptr)
{
	/* TODO: Implement free(). */
	if (ptr == NULL)
		return;
	struct mem_list *l = mem_list_find(ptr);
	if (l != NULL) {
		size_t size = l ->len;
		munmap(ptr, size);
		mem_list_del(ptr);
	}
}

void *realloc(void *ptr, size_t size)
{
	/* TODO: Implement realloc(). */
	if (!ptr) {
		return malloc(size);
	}
	if (size == 0) {
		free(ptr);
		return NULL;
	}
	struct mem_list *l = mem_list_find(ptr);
	void *p = malloc(size);
	if (p == NULL)
		return NULL;
	memcpy(p, ptr, l->len);
	free(ptr);
	return p;
}

void *reallocarray(void *ptr, size_t nmemb, size_t size)
{
	/* TODO: Implement reallocarray(). */
	// prevent overflow
	if (size * nmemb > __SIZE_MAX__)
		return NULL;
	size_t value = nmemb * size;
	return realloc(ptr, value);
}
