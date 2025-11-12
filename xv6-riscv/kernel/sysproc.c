#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}


uint64
sys_thread_create(void)
{
  // CSE 536: (Task 2.2.1) - Handle argument passing for thread_create()
  uint64 start_func;
  uint64 arg;
  
  argaddr(0, &start_func); 
  argaddr(1, &arg);        
  
  struct proc *p = myproc();
  struct thread *t = 0;
  int tid = -1;
  
  acquire(&p->lock);
  
  for(int i = 0; i < MAXTHREADS; i++) {
    if(p->thread[i].state == UNUSED) {
      t = &p->thread[i];
      tid = i;
      t->state = USED;
      break;
    }
  }
  
  if(tid == -1) {
    release(&p->lock);
    return -1;
  }
  

  memmove(t->trapframe, mythread()->trapframe, sizeof(struct trapframe));
  
  t->trapframe->epc = start_func; 
  t->trapframe->a0 = arg;        
  

  uint64 stacks_base = p->sz - (uint64)MAXTHREADS * 2 * PGSIZE;
  uint64 stack_page = stacks_base + (uint64)(tid * 2 + 1) * PGSIZE;
  t->trapframe->sp = stack_page + PGSIZE;  
  
  memset(&t->context, 0, sizeof(t->context));
  t->context.ra = (uint64)forkret;  
  t->context.sp = KSTACK(p->pid, tid) + PGSIZE; 
  
  t->trapframe->s11 = TRAPFRAME(tid);
  
  t->trapframe->kernel_satp = r_satp();
  t->trapframe->kernel_sp = KSTACK(p->pid, tid) + PGSIZE;
  t->trapframe->kernel_trap = (uint64)usertrap;
  t->trapframe->kernel_hartid = r_tp();
  
  t->priority = 0;
  t->chan = 0;
  
  t->state = RUNNABLE;
  
  p->active_threads++;
  
  release(&p->lock);
  
  return tid;  // Return the thread ID
}

uint64
sys_get_active_threads(void)
{
  return get_active_threads();
}

uint64
sys_thread_destroy(void)
{
  thread_destroy();
  return 0;
}

uint64
sys_get_tid(void)
{
  return get_tid();
}

