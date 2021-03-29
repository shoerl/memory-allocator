#include "xmalloc.h"
#include <sys/mman.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

typedef struct page_header page_header;

struct page_header {
	size_t size; // 8 bytes
	page_header* next; // 8 bytes
	int bitmap[16]; // 64 bytes
};

typedef struct special_page_header {
	size_t size; // 8 bytes
	size_t proof; // used as proof that its a special header
} special_page_header;

// if smallest size is 8, then most bytes we need is 64, which is 16 ints
const int BITMAP_LENGTH = 16;
const int BITS_PER_INT = 8 * sizeof(int);
const int BIGGEST_SIZE = 4016;
// sizes for easy lookup
const size_t sizes[19] = { 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512,
	768, 1024, 1536, 2048, 3192, 4016 };
// array of pointers to page headers
static page_header* bins[19];
const size_t PAGE_SIZE = 4096;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;


// 20 buckets = { 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 
// 384, 512, 768, 1024, 1536, 2048, 3192, 4016}



int
find_bucket_index(size_t size)
{
	int ii = 0;
	while(ii != 19) {
		if (size <= sizes[ii]) {
			return ii;
		}
		ii++;
	}
	return -1;
}

size_t
find_bucket_size(int idx)
{
	if (idx < 0 || idx >= 19) {
		return -1;
	}
	return sizes[idx];

}

uintptr_t
find_closest_pointer(uintptr_t ptr)
{
	// Round down to closest 4096-byte boundary
	return ptr &= -PAGE_SIZE;
}

/*
int
round_up_divison(int x, int y)
{
	return x/y + (x % y != 0);
}

int
amount_of_blocks(size_t bytes)
{
	// 4096 - 16
	int size = PAGE_SIZE - sizeof(size_t) - sizeof(page_header*) ;
	// 4080 / size of each block
	int first_guess = size / bytes;
	// (size of each block * amount of blocks) + (rounded up (amount of blocks / 32))
	int total = (bytes * first_guess) + (sizeof(int) * round_up_divison(first_guess, BITS_PER_INT));
	while (total > size) {
		first_guess--;
		total = (bytes * first_guess) + (sizeof(int) * round_up_divison(first_guess, BITS_PER_INT));
	}
	printf("%i\n", first_guess);
	return first_guess;
}
*/

int
amount_of_blocks(size_t bytes)
{
	//4096 - 120
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
init_header(size_t bytes, page_header* passed)
{
	//TODO: add case where we set next to a another page header
	page_header* header = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE,
	MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	header->size = bytes;
	int amount = amount_of_blocks(bytes);
	for (int i = 0; i < BITMAP_LENGTH; i++) {
		// set every int to all 1
		header->bitmap[i] = -1;
	}
	// 0 is free, 1 is full/unusable
	for (int j = 0; j < amount; j++) {
		toggle_bitmap(header, j);
	}

	header->next = 0;
	if (passed == 0) {
		// this is first one, put in spot in bin
		bins[find_bucket_index(bytes)] = header;
	} else {
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
	//printf("%li\n", header->size);
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
	pthread_mutex_lock(&(lock));
	if (bytes > BIGGEST_SIZE) {
		bytes += sizeof(special_page_header);
		special_page_header* sph = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		sph->size = bytes;
		// my birthday :)
		sph->proof = 05152000;
		pthread_mutex_unlock(&(lock));
		return ((void*) sph) + sizeof(special_page_header);
	}
	// figure out which bucket to go to
	int bucket = find_bucket_index(bytes);
	page_header* header = get_usable_header(bins[bucket]);
	// should be 80
	size_t offset = sizeof(page_header);
	// if there is no header with space
	if (header == 0) {
		header = init_header(find_bucket_size(bucket), bins[bucket]);
		// since we now passing first block
		toggle_bitmap(header, 0);
		pthread_mutex_unlock(&(lock));
		return ((void*) header) + offset;
	}
	int first_free = find_first_free(header);
	toggle_bitmap(header, first_free);
	// we know there is at least 1 free space
	pthread_mutex_unlock(&(lock));
	return ((void*) header) + offset + (first_free * header->size);
	
}

int
can_remap(page_header* header) {
	int numblocks = amount_of_blocks(header->size);
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

	for (int ii = 0; ii < numblocks; ii++) {
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
	// this is not a good way of doing it but whatever
	pthread_mutex_lock(&(lock));
	void* ptr_b = ptr - sizeof(size_t);
	size_t thesize = *((size_t*) ptr_b);
	if (thesize == 05152000) {
		ptr_b -= sizeof(size_t);
		size_t size = *((size_t*) ptr_b);
		munmap(ptr_b, size);
		pthread_mutex_unlock(&(lock));
		return;
	}
	uintptr_t pt = find_closest_pointer((uintptr_t) ptr);
	page_header* header = (page_header*) pt;
	// to calculate index of spot to free
	long idx = (((uintptr_t) ptr - pt) - sizeof(page_header)) / header->size;
	// toggle the bitmap at that index
	toggle_bitmap(header, idx);
	if (can_remap(header)) {
		if (header->next == 0) {
			bins[find_bucket_index(header->size)] = 0;
		} else {
			bins[find_bucket_index(header->size)] = header->next;
		}
		munmap(header, PAGE_SIZE);

	}
	pthread_mutex_unlock(&(lock));
}

void*
xrealloc(void* prev, size_t bytes)
{
	void* new_space = xmalloc(bytes);
	memcpy(new_space, prev, bytes);
	xfree(prev);
	return new_space;
}

