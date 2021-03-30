#include "xmalloc.h"
#include <sys/mman.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

typedef struct page_header page_header;

struct page_header {
	size_t size; // 8 bytes
	page_header* next; // 8 bytes
	page_header* prev; // 8 bytes
	int bitmap[16]; // 64 bytes
	int tidx; // 4 bytes
};

typedef struct special_page_header {
	size_t size; // 8 bytes
	size_t proof; // used as proof that its a special header
} special_page_header;

// if smallest size is 8, then most bytes we need is 64, which is 16 ints
const int BITMAP_LENGTH = 16;
const int BITS_PER_INT = 8 * sizeof(int);
const int BIGGEST_SIZE = 3192;
// sizes for easy lookup
const size_t sizes[18] = { 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3192 };
// array of pointers to page headers
static page_header* bins[18][4];
// initialize all threads to pthread initializer
static pthread_mutex_t locks[4] = { PTHREAD_MUTEX_INITIALIZER };
const size_t PAGE_SIZE = 4096;
//static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;


// 18 buckets = { 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 
// 384, 512, 768, 1024, 1536, 2048, 3192 }

// 0 is free, 1 is full/unusable
// so if int = 0, whole page is free
// if int = -1, whole page is full

// taken from Nat Tuck's notes from this semester
void
assert_ok(long rv, const char* call)
{
    if (rv == -1) {
        perror(call);
        exit(1);
    }
}

// gets the smallest bucket able to hold to the given size or return -1
int
find_bucket_index(size_t size)
{
	int ii = 0;
	while(ii < 18) {
		if (size <= sizes[ii]) {
			return ii;
		}
		ii++;
	}
	return -1;
}

// gets the block size of the bucket at the given index
size_t
find_bucket_size(int idx)
{
	if (idx < 0 || idx >= 18) {
		return -1;
	}
	return sizes[idx];

}

// gets the header of the page that holds the given ptr
uintptr_t
find_closest_pointer(uintptr_t ptr)
{
	// Round down to closest 4096-byte boundary
	return ptr &= -PAGE_SIZE;
}


int
amount_of_blocks(size_t bytes)
{
	//4096 - 92 because part of the page contains metadata
	int size = PAGE_SIZE - sizeof(page_header);
	// integer divison rounds down so we good
	return size / bytes;

}


void toggle_bitmap(page_header* header, int idx)
{
	int num = idx / BITS_PER_INT;
	int b_idx = idx % BITS_PER_INT;
	header->bitmap[num] ^= (1 << b_idx);
}


page_header*
init_header(size_t bytes, page_header* passed, int tidx, int bucketidx)
{
	//TODO: add case where we set next to a another page header
	page_header* header = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE,
	MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	assert_ok((long) header, "mmap");
	header->size = bytes;
	header->tidx = tidx;
	int amount = amount_of_blocks(bytes);
	int extra = (BITS_PER_INT * BITMAP_LENGTH) - amount;
	int leftover = extra / BITS_PER_INT + (extra % BITS_PER_INT != 0);
	int idx_bad = BITMAP_LENGTH - leftover;

	for (int i = 0; i < idx_bad; i++) {
		// set every int to all 1
		header->bitmap[i] = 0;
	}

	for (int i = idx_bad; i < BITMAP_LENGTH; i++) {
		// set every int to all 1
		header->bitmap[i] = -1;
	}
	int start = BITS_PER_INT * idx_bad;
	// 0 is free, 1 is full/unusable
	for (int j = start; j < amount; j++) {
		toggle_bitmap(header, j);
	}

	header->next = 0;
	if (passed == 0) {
		header->prev = 0;
		// this is first one, put in spot in bin
		bins[bucketidx][tidx] = header;
	} else {
		header->prev = passed;
		passed->next = header;
	}

	return header;

}

// essentially 0 if no space or header not initalized, otherwise
// returns pointer to first page_header that has space
page_header*
get_usable_header(page_header* header) {
	// if we pass it empty bin, get empty bin back
	if (header == 0) {
		return 0;
	}
	for (int ii = 0; ii < BITMAP_LENGTH; ii++) {
		if (header->bitmap[ii] != -1) {
			return header;
		}
	}
	if (header->next) {
		return get_usable_header(header->next);
	} else {
		return 0;
	}
}

int
find_first_free(page_header* header)
{
	for(int ii = 0; ii < amount_of_blocks(header->size); ii++) {
		int num = header->bitmap[ii / BITS_PER_INT];
		int idx = ii % BITS_PER_INT;
		if (num == -1) {
			continue;
		} else if ((num >> idx) % 2 == 0) {
			return ii;
		}
	}
	return -1;
}

void*
xmalloc(size_t bytes)
{
	
	if (bytes > BIGGEST_SIZE) {
		bytes += sizeof(special_page_header);
		special_page_header* sph = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		assert_ok((long) sph, "mmap");
		sph->size = bytes;
		// my birthday :)
		sph->proof = 19405152000;
		return ((void*) sph) + sizeof(special_page_header);
	}
	int tidx = rand() % 4;
	pthread_mutex_lock(&(locks[tidx]));
	// figure out which bucket to go to
	int bucket = find_bucket_index(bytes);
	page_header* header = get_usable_header(bins[bucket][tidx]);
	// should be 80
	size_t offset = sizeof(page_header);
	// if there is no header with space
	if (header == 0) {
		header = init_header(find_bucket_size(bucket), header, tidx, bucket);
		// since we now passing first block
		toggle_bitmap(header, 0);
		pthread_mutex_unlock(&(locks[tidx]));
		return ((void*) header) + offset;
	}
	int first_free = find_first_free(header);
	toggle_bitmap(header, first_free);
	// we know there is at least 1 free space
	pthread_mutex_unlock(&(locks[tidx]));
	return ((void*) header) + offset + (first_free * header->size);
	
}

int
can_remap(page_header* header) {
	// amount of entries that are real
	int numblocks = amount_of_blocks(header->size);
	// extra entires that are default set to 1
	int extra = (BITS_PER_INT * BITMAP_LENGTH) - numblocks;
	int leftover = extra / BITS_PER_INT + (extra % BITS_PER_INT != 0);
	int idx_bad = BITMAP_LENGTH - leftover;
	// TODO: optimize this, there are plenty of ways
	// if numblocks > extra then we can make sure first however many pages are 
	for (int ii = 0; ii < idx_bad; ii++) {
		if (header->bitmap[ii] != 0) {
			return 0;
		}
	}

	int start = (idx_bad * BITS_PER_INT);
	for (int ii = start; ii < numblocks; ii++) {
		int num = header->bitmap[ii / BITS_PER_INT];
		int idx = ii % BITS_PER_INT;
		if ((num >> idx) % 2 != 0) {
			return 0;
		}
	}

	return 1;

}

void
xfree(void* ptr)
{
	void* ptr_b = ptr - sizeof(size_t);
	size_t thesize = *((size_t*) ptr_b);
	if (thesize == 19405152000) {
		ptr_b -= sizeof(size_t);
		size_t size = *((size_t*) ptr_b);
		assert_ok(munmap(ptr_b, size), "munmap");
		return;
	}
	uintptr_t pt = find_closest_pointer((uintptr_t) ptr);
	page_header* header = (page_header*) pt;
	int tidx = header->tidx;
	pthread_mutex_lock(&(locks[tidx]));
	// to calculate index of spot to free
	long idx = (((uintptr_t) ptr - pt) - sizeof(page_header)) / header->size;
	// toggle the bitmap at that index
	toggle_bitmap(header, idx);
	if (can_remap(header)) {
		int hdr_idx = find_bucket_index(header->size);
		if (header->prev == 0) {
			bins[hdr_idx][tidx] = header->next;
			if (header->next) {
				(header->next)->prev = 0;
			}
		} else {
			(header->prev)->next = header->next;
			if (header->next) {
				(header->next)->prev = header->prev;
			}
		}
		assert_ok(munmap(header, PAGE_SIZE), "munmap");

	}
	pthread_mutex_unlock(&(locks[tidx]));
}

void*
xrealloc(void* prev, size_t bytes)
{
	uintptr_t pt = find_closest_pointer((uintptr_t) prev);
	page_header* header = (page_header*) pt;
	size_t size = header->size;
	void* new_space = xmalloc(bytes);
	memcpy(new_space, prev, size);
	xfree(prev);
	return new_space;
}