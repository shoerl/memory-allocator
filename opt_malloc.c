#include "xmalloc.h"
#include <sys/mman.h>
#include <string.h>
#include <math.h>

static free_block* bins[11];
const size_t PAGE_SIZE = 4096;

// 11 buckets = { 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 }

// to calculate size of bucket 2^(i + 2)

free_block* xpop(int bucket)
{
	free_block* first = bins[bucket];
	if (first->next) {
		bins[bucket] = first->next;
	} else {
		bins[bucket] = NULL;
	}
	return first;

}

void*
xmalloc(size_t bytes)
{
	// log_base2(bytes) - 2 (rounded up)
	int bucket = ceil(log2(bytes) - 2);
	if (bytes < 4) {
		bucket = 0;
	}
	if (bins[bucket]) {
		// 2 ^ (i + 2) - add 8 to each block so there is space
		int sizeofblock = pow(2, bucket + 2);
		// amount of free blocks in bucket = PAGE_SIZE / sizeofblock
		int amount = PAGE_SIZE / sizeofblock;
		// map new page
		void* ptr = mmap(NULL, PAGE_SIZE + (amount * sizeof(size_t)),
		 	PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

		sizeofblock += sizeof(size_t);
		// init first free block
		free_block* curr = ptr + sizeof(size_t);
		// set that bucket to point to first free block
		bins[bucket] = curr;
		// already set first one so start at 1 instead of 0
		for (int i = 1; i < amount; i++) {
			// increment ptr
			void* otherptr = ptr + (i * sizeofblock);
			// set current to point at next block
			curr->next = (free_block*) otherptr;
			// set curr for next iteration of loop
			curr = curr->next;
		}
	} 
	return (void*) xpop(bucket);
}

void
xfree(void* ptr)
{
	// TODO: write an optimized free
	free_block* block = ptr - sizeof(size_t);
	size_t size = block->size;
	int bucket = log2(size) - 2;
	block->next = bins[bucket];
	bins[bucket] = block;

}

void*
xrealloc(void* prev, size_t bytes)
{
    void* old_ptr = prev; // the name prev is confusing to me
    size_t current_size = (size_t) (old_ptr - sizeof(size_t)); //todo: get size
    int bucket = ceil(log2(bytes) - 2);
    int curr_bucket = ceil(log2(current_size) - 2);
    if (bucket == curr_bucket) {
    	return old_ptr;
    }
    void* new_ptr = xmalloc(bytes);
    memcpy(new_ptr, old_ptr, current_size);
    xfree(old_ptr);
	return new_ptr;
}
