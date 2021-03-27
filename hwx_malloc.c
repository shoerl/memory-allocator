#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "xmalloc.h"
#include <assert.h>

// TODO: This file should be replaced by another allocator implementation.
//
// If you have a working allocator from the previous HW, use that.
//
// If your previous homework doesn't work, you can use the provided allocator
// taken from the xv6 operating system. It's in xv6_malloc.c
//
// Either way:
//  - Replace xmalloc and xfree below with the working allocator you selected.
//  - Modify the allocator as nessiary to make it thread safe by adding exactly
//    one mutex to protect the free list. This has already been done for the
//    provided xv6 allocator.
//  - Implement the "realloc" function for this allocator.

const size_t PAGE_SIZE = 4096;
static hm_stats stats; // This initializes the stats to 0.
free_block* free_list = NULL;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

long
free_list_length()
{
	long ii = 0;
	free_block* head;
	for (head=free_list; head != NULL; head=head->next) {
		ii += 1;
	}
    return ii;
}

hm_stats*
hgetstats()
{
    stats.free_length = free_list_length();
    return &stats;
}

void
hprintstats()
{
    stats.free_length = free_list_length();
    fprintf(stderr, "\n== husky malloc stats ==\n");
    fprintf(stderr, "Mapped:   %ld\n", stats.pages_mapped);
    fprintf(stderr, "Unmapped: %ld\n", stats.pages_unmapped);
    fprintf(stderr, "Allocs:   %ld\n", stats.chunks_allocated);
    fprintf(stderr, "Frees:    %ld\n", stats.chunks_freed);
    fprintf(stderr, "Freelen:  %ld\n", stats.free_length);
}

static
size_t
div_up(size_t xx, size_t yy)
{
    // This is useful to calculate # of pages
    // for large allocations.
    size_t zz = xx / yy;

    if (zz * yy == xx) {
        return zz;
    }
    else {
        return zz + 1;
    }
}

void
coalesce()
{
	free_block* prev = free_list;
	if (prev == NULL) {
		return;
	}
	free_block* head = free_list->next;
	int count = 0;
	for (; head != NULL; head=head->next) {
		void* tmp = ((void*) prev) + prev->size;
		// if these are adjacent
		if ((uintptr_t) tmp == (uintptr_t) head) {
			count++;
			// add size to prev
			prev->size += head->size;
			// set next of prev to next of current (aka remove current from list)
			prev->next = head->next;
		}
		// set prev for next iteration
		prev = head;
	}
	if (count > 0) {
		coalesce();
	}
}


void
insert(free_block* to_insert)
{
	free_block* head;
	free_block* prev = NULL;
	for (head = free_list; head != NULL; head=head->next) {
		// if these are adjacent
		if ((uintptr_t) to_insert < (uintptr_t) head) {
			if (prev != NULL) {
				prev->next = to_insert;
			}
			to_insert->next = head;
			break;
		}
		// set prev for next iteration
		prev = head;
	}
	// if we are inserting it to first slot fix free list pointer
	if (prev == NULL) {
		free_list = to_insert;
	}
}

void
add_or_discard(free_block* curr, free_block* prev)
{
	if (curr->size > sizeof(free_block)) {
		if (prev == NULL) {
			// if we are messing with head of list
			free_list = curr;
		} else {
			prev->next = curr;
		}
	} else {
		if (prev == NULL) {
			free_list = NULL;
		} else {
			prev->next = curr->next;
		}
	}
}

void*
xmalloc(size_t size)
{
	int ret = pthread_mutex_lock(&lock);
	assert(ret != -1);
 	size += sizeof(size_t);
	stats.chunks_allocated += 1;
	if (size < PAGE_SIZE) {
		free_block* prev = NULL;
		free_block* curr = free_list;
		for (; curr != NULL; curr=curr->next) {
			if (curr->size >= size) {
				size_t oldSize = curr->size;
				block_header* block = (void*) curr;
				block->size = size;
				curr = ((void*) curr) + size;
				curr->size = oldSize - size;
				add_or_discard(curr, prev);
				ret = pthread_mutex_unlock(&lock);
				assert(ret != -1);
				return ((void*) block) + sizeof(size_t); 

			}
			prev = curr;
		}
		// no more space, map extra page 
		stats.pages_mapped += 1;
		block_header* new_header = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		new_header->size = size;
		free_block* new_block = ((void*) new_header) + size;
		new_block->size = PAGE_SIZE - size;
		if (new_block->size > sizeof(free_block)) {
			insert(new_block);
		}
		ret = pthread_mutex_unlock(&lock);
		assert(ret != -1);
		return ((void*) new_header) + sizeof(size_t);
	} else {
		void* block = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		((block_header*) block)->size = size - sizeof(size_t);
		stats.pages_mapped += div_up(size, PAGE_SIZE);
		ret = pthread_mutex_unlock(&lock);
		assert(ret != -1);
		return block + sizeof(size_t);
	}
}

void
xfree(void* item)
{
	int ret = pthread_mutex_lock(&lock);
	assert(ret != -1);
    // TODO: replace this with free
    stats.chunks_freed += 1;
    void* item_f = item - sizeof(size_t);
    size_t size = *((size_t*) item_f);
    // to account for size we keep track of
    if (size < PAGE_SIZE) {
    	free_block* new = item_f;
    	new->size = size;
    	insert(new);
    	coalesce();
    } else {
    	size += sizeof(size_t);
    	stats.pages_unmapped += div_up(size, PAGE_SIZE);
    	munmap(item_f, size);
    }
    ret = pthread_mutex_unlock(&lock);
    assert(ret != -1);
}

void*
xrealloc(void* prev, size_t bytes)
{
	prev = prev - sizeof(size_t);
	block_header* prev_block = prev;
	size_t prev_size = prev_block->size;
	if (bytes == prev_size) {
		return ((void*) prev_block);
	}
	void* tmp = ((void*) prev) + prev_block->size;
	free_block* other_prev = NULL;
	for (free_block* head = free_list; head != NULL; head=head->next) {
		// if these are adjacent
		// TODO: if extra space in free block head, break it up
		if ((uintptr_t) tmp == (uintptr_t) head && bytes <= (head->size + prev_size)) {
			size_t space_needed = bytes - prev_size;
			size_t leftover_space = head->size - space_needed;
			prev_block->size += space_needed;
			head = ((void*) head) + space_needed;
			head->size = leftover_space;
			add_or_discard(head, other_prev);
			return prev + sizeof(size_t);
		}
		// set prev for next iteration
		other_prev = head;
	}
    void* new_space = xmalloc(bytes);
    memcpy(new_space, prev, prev_size);
    ((block_header*) new_space)->size = bytes;
    xfree(prev + sizeof(size_t));
    return new_space + sizeof(size_t);
}

/*
int
main(int argc, char* argv[])
{
    long* xs = xmalloc(30 * sizeof(long));
    for (int i = 0; i < 30; i++) {
    	xs[i] = i + 1;
    }
    xs = xrealloc(xs, 50 * sizeof(long));
    for (int i = 0; i < 30; i++) {
    	printf("at index %i : %li\n", i, xs[i]);
    }
	xs[32] = 500;
	printf("%li\n", xs[32]);
	hprintstats();
	xfree(xs);
}
*/