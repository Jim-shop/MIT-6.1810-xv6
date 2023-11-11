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
  int ref_cnt[(PHYSTOP - KERNBASE) / PGSIZE];
} kmem;

int*
ref_cnt(void *pa)
{
  return &kmem.ref_cnt[(uint64)(pa - KERNBASE) / PGSIZE];
}

void
inc_ref_cnt(void *pa)
{
  acquire(&kmem.lock);
  ++*ref_cnt(pa);
  release(&kmem.lock);
}

void
dec_ref_cnt(void *pa)
{
  acquire(&kmem.lock);
  --*ref_cnt(pa);
  release(&kmem.lock);
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    acquire(&kmem.lock);
    *ref_cnt(p) = 1;
    release(&kmem.lock);
    kfree(p);
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

  acquire(&kmem.lock);
  int* pa_ref_cnt = ref_cnt(pa);
  // printf("pa=%p, *ref_cnt=%d\n", pa, *pa_ref_cnt);
  if (*pa_ref_cnt <= 0) {
    panic("kfree");
  }
  // Only release pages with ref_cnt == 1
  if (--*pa_ref_cnt > 0) {
    release(&kmem.lock);
    return;
  }
  release(&kmem.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    *ref_cnt(r) = 1;
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  return (void*)r;
}
