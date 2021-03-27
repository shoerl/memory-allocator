#ifndef XMALLOC_H
#define XMALLOC_H

#include <stddef.h>

typedef struct free_block free_block;

struct free_block {
	size_t size;
	free_block* next;
};

typedef struct block_header {
	size_t size;
} block_header;

void* xmalloc(size_t bytes);
void  xfree(void* ptr);
void* xrealloc(void* prev, size_t bytes);

#endif
