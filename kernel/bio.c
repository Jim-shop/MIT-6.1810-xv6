// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 13
struct {
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[NBUCKETS];
} bcache;

static inline int
bcache_bucket_hash(int dev, int blockno)
{
  return (dev + blockno) % NBUCKETS;
}

void
binit(void)
{
  struct buf *b;

  for (int i = 0; i < NBUCKETS; i++) {
    initlock(&bcache.lock[i], "bcache");
    // Create linked list of buffers
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }

  for (int i = 0; i < NBUF; i++)
  {
    int hash = i % NBUCKETS; // randomly init
    b = bcache.buf + i;
    b->next = bcache.head[hash].next;
    b->prev = &bcache.head[hash];
    initsleeplock(&b->lock, "buffer");
    bcache.head[hash].next->prev = b;
    bcache.head[hash].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int hash = bcache_bucket_hash(dev, blockno);

  acquire(&bcache.lock[hash]);

  // Is the block already cached?
  for(b = bcache.head[hash].next; b != &bcache.head[hash]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[hash]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  // First look for free buf in the same bucket.
  for(b = bcache.head[hash].prev; b != &bcache.head[hash]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[hash]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Then look for free buf in other bucket and take it.
  release(&bcache.lock[hash]);
  struct buf *steal_buf;
  for (int i = 1; i < NBUCKETS; i++) {
    int other = (hash + i) % NBUCKETS;
    acquire(&bcache.lock[other]);
    for (b = bcache.head[other].prev; b != &bcache.head[other]; b = b->prev){
      if(b->refcnt == 0) {
        b->prev->next = b->next;
        b->next->prev = b->prev;
        release(&bcache.lock[other]);
        steal_buf = b;
        goto found;
      }
    }
    release(&bcache.lock[other]);
  }
  panic("bget: no buffers");

found:
  // Try to insert free buf into self bucket.
  acquire(&bcache.lock[hash]);

  // During the period when holding no lock, is the block already cached in my buf?
  for(b = bcache.head[hash].next; b != &bcache.head[hash]; b = b->next){
    if(b->dev == dev && b->blockno == blockno) {
      // if is cache as `b`, use `b`, simply insert `steal_buf` before `b`
      b->refcnt++;
      steal_buf->prev = b->prev;
      steal_buf->next = b;
      b->prev->next = steal_buf;
      b->prev = steal_buf;
      release(&bcache.lock[hash]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // else then push steal_buf to my buf and activate it
  steal_buf->dev = dev;
  steal_buf->blockno = blockno;
  steal_buf->valid = 0;
  steal_buf->refcnt = 1;
  steal_buf->next = &bcache.head[hash];
  steal_buf->prev = bcache.head[hash].prev;
  steal_buf->prev->next = steal_buf;
  steal_buf->next->prev = steal_buf;
  release(&bcache.lock[hash]);
  acquiresleep(&steal_buf->lock);
  return steal_buf;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int hash = bcache_bucket_hash(b->dev, b->blockno);

  acquire(&bcache.lock[hash]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[hash].next;
    b->prev = &bcache.head[hash];
    bcache.head[hash].next->prev = b;
    bcache.head[hash].next = b;
  }
  
  release(&bcache.lock[hash]);
}

void
bpin(struct buf *b) {
  int hash = bcache_bucket_hash(b->dev, b->blockno);
  acquire(&bcache.lock[hash]);
  b->refcnt++;
  release(&bcache.lock[hash]);
}

void
bunpin(struct buf *b) {
  int hash = bcache_bucket_hash(b->dev, b->blockno);
  acquire(&bcache.lock[hash]);
  b->refcnt--;
  release(&bcache.lock[hash]);
}


