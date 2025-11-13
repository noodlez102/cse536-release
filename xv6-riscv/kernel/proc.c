#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

enum sched_algo { ROUND_ROBIN, PRIORITY };
enum sched_algo SCHED_ALGO = ROUND_ROBIN;

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  // CSE 536: (Task 2.1.1) - Allocate and map MAXTHREADS kernel stacks for each process according to the instructions
  struct proc *p;
  // printf("entering proc_mapstacks\n");
  for(p = proc; p < &proc[NPROC]; p++) {
    for (int tid= 0; tid < MAXTHREADS; tid++) {
      char *pa = kalloc();
      if (pa == 0)
        panic("proc_mapstacks: kalloc");
      memset(pa, 0, PGSIZE);
      uint64 va = KSTACK((int) (p - proc), tid);
      kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
    }
  }
  // printf("exiting proc_mapstacks\n");

}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      // CSE 536: Initialize the state of all threads in this process
      thread_init(p);
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

// CSE 536: Return the current struct thread *, or zero if none.
struct thread*
mythread(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct thread *t = c->thread;
  pop_off();
  return t;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  struct thread *t;

  for(p = proc; p < &proc[NPROC]; p++) {
    t = &p->thread[0];
    acquire(&p->lock);
    // CSE 536: At allocproc time, there's only 1 thread for the process.
    // Hence, check the first thread's state, instead of the rest.
    if(t->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  // CSE 536: just use the index of this process in the proc array as the pid
  p->pid = (int)(p - proc);
  t->state = USED;

  // CSE 536: (Task 2.1.1) - Allocate MAXTHREAD trapframes
  // printf("entering allocproc\n");
  for(int tid = 0; tid < MAXTHREADS; tid++) {
    p->thread[tid].tid = tid;
    p->thread[tid].state = USED;
    p->thread[tid].priority = 0;
    p->thread[tid].chan = 0;
    
    if((p->thread[tid].trapframe =((struct trapframe*)kalloc()))== 0) {
      freeproc(p);
      release(&p->lock);
      return 0;
    }
    // p->thread[tid].trapframe->epc = r_sepc();
    memset(p->thread[tid].trapframe, 0, PGSIZE);
  }
  // printf("exiting allocproc\n");

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  // printf("size of t is %d, and address is %p\n",sizeof(t->context),&(t->context));
  memset(&(t->context), 0, sizeof(t->context));
  t->context.ra = (uint64)forkret;
  t->context.sp = KSTACK(p->pid,t->tid) + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  struct thread *t;

  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->killed = 0;
  p->xstate = 0;
  
  // CSE 536: Reset thread state
  for(t = p->thread; t < &p->thread[MAXTHREADS]; t++) {
    t->state = UNUSED;
    t->chan = 0;
    t->priority = 0;
  }
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // CSE 536: (Task 2.1.1) - Map the MAXTHREAD trapframes right below the Trampoline as mentioned in the instructions
  // printf("entering proc_pagetable\n");

  for(int i = 0; i < MAXTHREADS; i++) {
    if (mappages(pagetable, TRAPFRAME(p->thread[i].tid), PGSIZE, (uint64)p->thread[i].trapframe, PTE_R | PTE_W ) < 0) {
      uvmunmap(pagetable, TRAMPOLINE, 1, 0);  
      uvmfree(pagetable, 0);
      return 0;
    }
  }
  // printf("exiting proc_pagetable\n");

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);

  // CSE 536: (Task 2.1.1) - unmap and free all the trapframes
  // printf("entering proc_freepagetable\n");
  // struct proc *p = myproc();
  for(int i = 0; i < MAXTHREADS; i++){
    uvmunmap(pagetable, TRAPFRAME(i), 1, 1); 
    // p->thread[i].trapframe=0;
  }
  // printf("exiting proc_freepagetable\n");
  uvmfree(pagetable, sz); 
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc   *p;
  struct thread *t;

  p = allocproc();
  initproc = p;
  t = &p->thread[0];
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  t->trapframe->epc = 0;      // user program counter
  t->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  t->state = RUNNABLE;
  t->priority = 0;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  // Adil: Shouldn't you fork from the thread that asked for the fork?
  // Changed, please check.
  *(np->thread[0].trapframe) = *(mythread()->trapframe);

  // Cause fork to return 0 in the child.
  np->thread[0].trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->thread[0].state = RUNNABLE;
  np->thread[0].priority = 0;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();
  struct thread *t = mythread();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  // acquire(&p->lock);
  acquire(&t->lock);

  p->xstate = status;
  // Make sure only the main thread calls exit() and waits for other threads to finish executing before doing so.
  p->thread[0].state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;

        // Adil: Technically, this should be done for each thread now.
        // Check/fix later. (minor)
        if(pp->thread[0].state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      if(SCHED_ALGO == ROUND_ROBIN)
        roundRobin(p);
      else if(SCHED_ALGO == PRIORITY)
        priorityScheduling(p);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct thread *t = mythread();

  // if(!holding(&p->lock))
  if(!holding(&t->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(t->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&t->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct thread *t = mythread();
  acquire(&t->lock);
  t->state = RUNNABLE;
  sched();
  release(&t->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&mythread()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct thread *t = mythread();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&t->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  // Adil: should chan be per-thread? (probably..)
  // Changed, check.
  t->chan = chan;

  // Adil: Shouldn't we check which thread it is and then go to sleep only on that thread?
  // Changed, please check.
  t->state = SLEEPING;

  sched();

  // Tidy up.
  t->chan = 0;

  // Reacquire original lock.
  release(&t->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;
  struct thread *t;

  // Adil: If we want to cater to sleep, we should make sure each 
  // thread goes to sleep.
  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      // Adil: Changed, check.
      for(t = p->thread; t < &p->thread[MAXTHREADS]; t++) {
        acquire(&t->lock);
        if(t->state == SLEEPING && t->chan == chan) {
          t->state = RUNNABLE;
        }
        release(&t->lock);
      }
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;
  // struct thread *t;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      // for(t = p->thread; t < &p->thread[MAXTHREADS]; t++) {
      //   acquire(&t->lock);
      //   if(t->state == SLEEPING) {
      //     // Wake thread from sleep().
      //     t->state = RUNNABLE;
      //   }
      //   release(&t->lock);
      // }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  // Adil: (Minor) might want to print out all the process/threads.
  // Since this is only for debugging, we can skip.
  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->thread[0].state == UNUSED)
      continue;
    if(p->thread[0].state >= 0 && p->thread[0].state < NELEM(states) && states[p->thread[0].state])
      state = states[p->thread[0].state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
