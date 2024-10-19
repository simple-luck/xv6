#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_exit(void) {
  int n;
  if (argint(0, &n) < 0) return -1;
  exit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return fork(); }

uint64 sys_wait(void) {
  uint64 p;
  int flags;
  if (argaddr(0, &p) < 0||argint(1,&flags)<0) return -1;
  return wait(p,flags);
}

uint64 sys_sbrk(void) {
  int addr;
  int n;

  if (argint(0, &n) < 0) return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0) return -1;
  return addr;
}

uint64 sys_sleep(void) {
  int n;
  uint ticks0;

  if (argint(0, &n) < 0) return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (myproc()->killed) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void) {
  int pid;

  if (argint(0, &pid) < 0) return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 sys_rename(void) {
  char name[16];
  int len = argstr(0, name, MAXPATH);
  if (len < 0) {
    return -1;
  }
  struct proc *p = myproc();
  memmove(p->name, name, len);
  p->name[len] = '\0';
  return 0;
}

uint64 sys_yield(void){
    struct proc *p=myproc();
    printf("Save the context of the process to the memory region from address %p to %p\n",(void *)&p->context, 
       (void *)((uint64)&p->context + sizeof(p->context)));
    printf("Current running process pid is %d and user pc is 0x%x\n", p->pid, p->trapframe->epc);

    int current_index = -1;  
    //获取当前进程在进程表中的索引
    for (int i = 0; i < NPROC; i++) {
        if (&proc[i] == myproc()) {  
          current_index = i; 
          break;  
        }
    }

    acquire(&p->lock);
    int found = 0;
    //环形遍历进程表
    for (int i = 0; i < NPROC; i++) {
      struct proc *pp = &proc[(current_index + i) % NPROC];
     
      if (pp->state == RUNNABLE && pp!=p) {
        
        printf("Next runnable process pid is %d and user pc is 0x%x\n",pp->pid, pp->trapframe->epc);
        found = 1;
       
        break;
      }
    }
    
    if (found == 0) {
      printf("No runnable process found!\n");
    }
    release(&p->lock);
    yield();
    return 0;
}
