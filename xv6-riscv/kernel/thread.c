#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

void thread_init(struct proc *p) {
    for(int i = 0; i < MAXTHREADS; i++) {
        initlock(&p->thread[i].lock, "thread_lock");
        p->thread[i].state = UNUSED;
        p->thread[i].priority = 0;
        p->thread[i].tid = i;
    }

    p->active_threads = 1;
}

void thread_create(uint64 entry_func, uint64 args[], int priority) { 
    // CSE 536: (Task 2.2.2) - Write the thread creation logic
    struct proc *p = myproc();
    struct thread *t = 0;
    // find an UNUSED thread
    for(int tid = 0; tid < MAXTHREADS; tid++) {
        if (p->thread[tid].state == UNUSED) {
            t = &p->thread[tid];
            t->state = RUNNABLE;
            t->priority = priority;
            t->tid = tid;
            break;
        }
    }
    if(t == 0)
        return;
    struct trapframe *tf = t->trapframe;
    // set correct user + kernel context
    tf->epc = entry_func;
    tf->a0 = args[0];
    tf->a1 = args[1];
    tf->a2 = args[2];
    tf->a3 = args[3];
    tf->a4 = args[4];
    tf->a5 = args[5];

    uint64 user_stack_top = TRAPFRAME(t->tid);
    tf->sp = user_stack_top;

    memset(&(t->context), 0, sizeof(t->context));
    t->context.ra = (uint64)forkret;
    t->context.sp = KSTACK(p->pid,t->tid) + PGSIZE;    

    p->active_threads++;
}

void thread_destroy() {
    // CSE 536: (Task 2.2.2) - Reset thread state + yield to scheduler
    struct proc *p = myproc();
    struct thread *t = mythread();

    acquire(&t->lock);
    t->state = UNUSED;
    if (p->active_threads > 0)
        p->active_threads--;

    release(&t->lock);
    yield();
}

int get_active_threads() {
    // CSE 536: (Task 2.2.2) - Get the total no of RUNNABLE threads of this process
    return myproc()->active_threads;
}

int get_tid() {
    // CSE 536: (Task 2.2.2) - Get TID of current thread
    return mythread()->tid;
}


// CSE 536: (Task 2.2.3) - Implement the scheduling algorithms
void roundRobin(struct proc *p) {
    struct cpu *c = mycpu();
    for(int i=0; i<MAXTHREADS;i++){
        struct thread *t = &p->thread[i]; // schedule the main thread of this process

        acquire(&t->lock);
        if(t->state == RUNNABLE) {
            t->state = RUNNING;
            c->proc = p;
            c->thread = t;
            c->tid = t->tid;

            print_schedule(p, t->tid);

            swtch(&c->context, &t->context);
            c->proc = 0;
            c->thread = 0;
            c->tid = -1;
        }
        release(&t->lock);
    }

}

void priorityScheduling(struct proc *p) {
}