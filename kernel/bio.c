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
  //struct buf head;
  struct spinlock alloc_lock; // 全局分配大锁
  struct buf *hashbucket[NBUCKETS]; //每个哈希队列一个linked list及一个lock
} bcache;

void
binit(void)
{
  for (int i = 0; i < NBUCKETS; i++) {
    initlock(&bcache.lock[i], "bcache");
    bcache.hashbucket[i] = 0; // 初始化哈希桶
  }
  initlock(&bcache.alloc_lock, "alloc_lock"); // 初始化全局分配大锁
  for (int i = 0; i < NBUF; i++) {
    bcache.buf[i].next =0;
    bcache.buf[i].refcnt = 0;
    initsleeplock(&bcache.buf[i].lock, "buffer");
  }
}

static uint hash(uint dev, uint blockno) {
  return (dev ^ blockno) % NBUCKETS; // 简单哈希函数
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  uint h = hash(dev, blockno);
  acquire(&bcache.lock[h]);

  // Is the block already cached?
  for (b = bcache.hashbucket[h]; b; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
         b->refcnt++;
          release(&bcache.lock[h]);
          acquiresleep(&b->lock);
          return b;
      }
  }
  
  // 未找到，释放桶锁并进入替换/分配流程
  release(&bcache.lock[h]); 
  acquire(&bcache.alloc_lock); // 获取全局分配大锁

  // 再次获取哈希桶的锁
  acquire(&bcache.lock[h]);

  // 检查是否仍未缓存
  for (b = bcache.hashbucket[h]; b; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.lock[h]);
      release(&bcache.alloc_lock); // 释放全局分配大锁
      acquiresleep(&b->lock);
      return b;
    }
  }
  //acquire(&bcache.lock[h]);
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for (int i = 0; i < NBUF; i++) {
    if (bcache.buf[i].refcnt == 0) {
      b = &bcache.buf[i];
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      b->next = bcache.hashbucket[h]; // 插入到哈希桶链表
      bcache.hashbucket[h] = b;
      release(&bcache.lock[h]);
      release(&bcache.alloc_lock); // 释放全局分配大锁
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.lock[h]);
  release(&bcache.alloc_lock); // 释放全局分配大锁
  panic("bget: no buffers");
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
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint h = hash(b->dev, b->blockno);
  acquire(&bcache.lock[h]); // 获取对应桶的锁

  // 在释放之前检查引用计数
  if (b->refcnt == 0) {
    return; // 结束程序
  }
  
  b->refcnt--;
  if (b->refcnt == 0) {
    // 从哈希桶中移除
    struct buf **prev = &bcache.hashbucket[h];
    while (*prev) {
      if (*prev == b) {
        *prev = b->next; // 从链表中移除
        break;
      }
      prev = &(*prev)->next;
    }
  }

  release(&bcache.lock[h]);
}

void
bpin(struct buf *b) {
   acquire(&bcache.lock[hash(b->dev, b->blockno)]);
  b->refcnt++;
  release(&bcache.lock[hash(b->dev, b->blockno)]);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock[hash(b->dev, b->blockno)]);
  b->refcnt--;
  release(&bcache.lock[hash(b->dev, b->blockno)]);
}