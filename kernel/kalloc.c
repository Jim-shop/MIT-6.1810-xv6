// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  for (int i = 0; i < NCPU; i++)
    initlock(&kmem[i].lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  int bucket_size = (pa_end - pa_start + NCPU - 1) / NCPU; // round up
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    memset(p, 1, PGSIZE); // Fill with junk.
    struct run *r = (struct run *)p;
    int i = ((void *)p - pa_start) / bucket_size;
    r->next = kmem[i].freelist;
    kmem[i].freelist = r;
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int i = cpuid();
  pop_off();
  acquire(&kmem[i].lock);
  r->next = kmem[i].freelist;
  kmem[i].freelist = r;
  release(&kmem[i].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int i = cpuid();
  pop_off();
  acquire(&kmem[i].lock);
  r = kmem[i].freelist;
  if(r) {
    kmem[i].freelist = r->next;
    release(&kmem[i].lock);
  } else {
    release(&kmem[i].lock);
    for (int j = 1; j < NCPU; j++) {
      int other = (i + j) % NCPU;
      acquire(&kmem[other].lock);
      if ((r = kmem[other].freelist)) {
        kmem[other].freelist = r->next;
        release(&kmem[other].lock);
        break;
      }
      release(&kmem[other].lock);
    }
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
