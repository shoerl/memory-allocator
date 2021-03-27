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

typedef long Align;

union header {
  struct {
    union header *ptr;
    unsigned int size;
  } s;
  Align x;
};

typedef union header Header;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Header base;
static Header *freep;

static
void
xfree_helper(void *ap)
{
  Header *bp, *p;

  bp = (Header*)ap - 1;
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break;
  if(bp + bp->s.size == p->s.ptr){
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
  } else
    bp->s.ptr = p->s.ptr;
  if(p + p->s.size == bp){
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
  } else
    p->s.ptr = bp;
  freep = p;
}

void
xfree(void* ap)
{
  pthread_mutex_lock(&lock);
  xfree_helper(ap);
  pthread_mutex_unlock(&lock);
}

static Header*
morecore(size_t nu)
{
  char *p;
  Header *hp;

  if(nu < 4096)
    nu = 4096;
  // TODO: Replace sbrk use with mmap
  p = mmap(0, nu * sizeof(Header), PROT_READ|PROT_WRITE,
           MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
  if(p == (char*)-1)
    return 0;
  hp = (Header*)p;
  hp->s.size = nu;
  xfree_helper((void*)(hp + 1));
  return freep;
}

void*
xmalloc(size_t nbytes)
{
  Header *p, *prevp;
  unsigned int nunits;

  pthread_mutex_lock(&lock);
  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;
  if((prevp = freep) == 0){
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  for(p = prevp->s.ptr; ; prevp = p, p = p->s.ptr){
    if(p->s.size >= nunits){
      if(p->s.size == nunits)
        prevp->s.ptr = p->s.ptr;
      else {
        p->s.size -= nunits;
        p += p->s.size;
        p->s.size = nunits;
      }
      freep = prevp;
      pthread_mutex_unlock(&lock);
      return (void*)(p + 1);
    }
    if(p == freep) {
      if((p = morecore(nunits)) == 0) {
        pthread_mutex_unlock(&lock);
        return 0;
      }
    }
  }
}

void*
xrealloc(void* prev, size_t nn)
{
	void* new_space = xmalloc(nn);
	memcpy(new_space, prev, nn);
	xfree(prev);
	return new_space;	
}
