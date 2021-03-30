#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

#include "xmalloc.h"

const size_t PAGE_SIZE = 4096;
free_block* free_list = NULL;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;


void
coalesce()
{
  free_block* prev = free_list;
  if (prev == NULL) {
    return;
  }
  free_block* head = free_list->next;
  if (head == 0) {
    return;
  }
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
  free_block* head = free_list;
  if (head == 0) {
    return;
  }
  free_block* prev = NULL;
  for (; head != NULL; head=head->next) {
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
      munmap(item_f, size);
    }
    ret = pthread_mutex_unlock(&lock);
    assert(ret != -1);
}


void*
xrealloc(void* prev, size_t nn)
{
	void* new_space = xmalloc(nn);
	memcpy(new_space, prev, nn);
	xfree(prev);
	return new_space;	
}
