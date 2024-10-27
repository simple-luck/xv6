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

struct kmem{
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmems[NCPU];

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    initlock(&kmems[i].lock, "kmem_lock"); // 初始化每个 CPU 的锁
    kmems[i].freelist = 0; // 初始化 freelist 为 NULL
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
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

  push_off(); // 关闭中断
  int cpu_id = cpuid(); // 获取当前 CPU ID
  acquire(&kmems[cpu_id].lock);
  r->next = kmems[cpu_id].freelist;
  kmems[cpu_id].freelist = r;
  release(&kmems[cpu_id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off(); // 关闭中断
  int cpu_id = cpuid(); // 获取当前 CPU ID

  // 优先尝试从当前 CPU 的 freelist 中分配内存块
  acquire(&kmems[cpu_id].lock); // 获取当前 CPU 的锁
  r = kmems[cpu_id].freelist; // 从当前 CPU 的 freelist 中获取
  if (r) {
      kmems[cpu_id].freelist = r->next; // 更新 freelist
      release(&kmems[cpu_id].lock);
      memset((char*)r, 5, PGSIZE); // 填充垃圾值
      pop_off(); // 开启中断
      return (void*)r;
  }
  release(&kmems[cpu_id].lock); // 释放锁

  // 当前 CPU 的 freelist 没有空闲块，尝试从其他 CPU 窃取
  for (int i = 0; i < NCPU; i++) {
        if (i != cpu_id) {
            acquire(&kmems[i].lock); // 获取其他 CPU 的锁
            if (kmems[i].freelist) { // 如果其他 CPU 有空闲块
                r = kmems[i].freelist; // 从其他 CPU 的 freelist 中窃取
                kmems[i].freelist = r->next; // 更新其他 CPU 的 freelist
                release(&kmems[i].lock); // 释放锁
                memset((char*)r, 5, PGSIZE); // 填充垃圾值
                pop_off(); // 开启中断
                return (void*)r;
            }
            release(&kmems[i].lock); // 释放锁
        }
  }

  pop_off(); // 开启中断
  return 0; // 所有 CPU 都没有空闲块 
  
}
