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
  struct spinlock glock;
  struct buf hashbucket[NBUCKETS]; //每个哈希队列一个linked list及一个lock
} bcache;


void
binit(void)
{
  struct buf* b;
  //printf("hello\n");
  for (int i = 0; i < NBUCKETS; i++) {
    initlock(&bcache.lock[i], "bcache");
    b=&bcache.hashbucket[i];
    b->next=b;
    b->prev=b;
  }
  initlock(&bcache.glock, "bcache_gloock");
  //printf("hello\n");
  // 初始化所有缓冲区并将它们添加到第一个哈希桶
  b=&bcache.hashbucket[0];
  for (int i = 0; i < NBUF; i++) {
    bcache.buf[i].next = b->next; // 将当前缓冲区的下一个指针指向当前哈希桶的头
    bcache.buf[i].prev=b;
    b->next->prev=&bcache.buf[i];
    b->next= &bcache.buf[i];// 将当前缓冲区添加到第一个哈希桶
    bcache.buf[i].refcnt = 0; // 初始化引用计数
    initsleeplock(&bcache.buf[i].lock, "buffer"); // 初始化锁
    
  }
}

static uint hash(uint dev, uint blockno) {
  return  blockno % NBUCKETS; // 简单哈希函数
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct buf* head;
  uint h = hash(dev, blockno);
  acquire(&bcache.lock[h]);
  //printf("hello\n");
  // Is the block already cached?
  head=&bcache.hashbucket[h];
  for (b = head->next; b!=head; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
          b->refcnt++;
          release(&bcache.lock[h]);
          acquiresleep(&b->lock);
          return b;
      }
  }
  
  for (b = head->next; b!=head; b = b->next) {
      if (b->refcnt == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt++;
      
        release(&bcache.lock[h]);
        acquiresleep(&b->lock);
        return b;
      }
  }
  release(&bcache.lock[h]);
  acquire(&bcache.glock);
  
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for (uint i = 0; i < NBUCKETS; i++) {
    if(i==h)continue;
    acquire(&bcache.lock[i]);
    
    head=&bcache.hashbucket[i];
    //printf("hello\n");
    for (b =bcache.hashbucket[i].next; b!=head; b = b->next) {
      if (b->refcnt == 0) {
        //printf("hello\n");
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt++;
        //printf("hello\n");
        b->prev->next=b->next;
        b->next->prev=b->prev;
        //printf("hello\n");
        release(&bcache.lock[i]);

        acquire(&bcache.lock[h]);
        head=&bcache.hashbucket[h];
        b->prev=head;
        b->next =head->next; // 插入到哈希桶链表
        head->next->prev=b;
        head->next=b;

        release(&bcache.lock[h]);
        release(&bcache.glock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    //printf("hello\n");
    release(&bcache.lock[i]);  
  }
  release(&bcache.glock);
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
  bunpin(b);
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