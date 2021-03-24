#ifndef HMALLOC_H
#define HMALLOC_H

// Husky Malloc Interface
// cs3650 Starter Code

// initialize reference so self-referential definition is allowed
typedef struct free_block free_block;

struct free_block {
	size_t size;
	free_block* next;
};

typedef struct block_header {
	size_t size;
} block_header;

typedef struct hm_stats {
    long pages_mapped;
    long pages_unmapped;
    long chunks_allocated;
    long chunks_freed;
    long free_length;
} hm_stats;

hm_stats* hgetstats();
void hprintstats();

void* hmalloc(size_t size);
void hfree(void* item);

#endif
