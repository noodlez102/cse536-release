/* This file contains code for a generic page fault handler for processes. */
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

int loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz);
int flags2perm(int flags);

/* CSE 536: (2.4) read current time. */
uint64 read_current_timestamp() {
  uint64 curticks = 0;
  acquire(&tickslock);
  curticks = ticks;
  wakeup(&ticks);
  release(&tickslock);
  return curticks;
}

bool psa_tracker[PSASIZE];

/* All blocks are free during initialization. */
void init_psa_regions(void)
{
    for (int i = 0; i < PSASIZE; i++) 
        psa_tracker[i] = false;
}

/* Evict heap page to disk when resident pages exceed limit */
void evict_page_to_disk(struct proc* p) {
    /* Find free block */
    int blockno = -1;
    for (int i = 0; i < PSASIZE - 3; i++) {
        if (!psa_tracker[i] && !psa_tracker[i+1] && !psa_tracker[i+2] && !psa_tracker[i+3]) {
            blockno = i;
            psa_tracker[i]=psa_tracker[i+1]=psa_tracker[i+2]=psa_tracker[i+3]= true;
            break;
        }
    }
    if (blockno == -1) {
        printf("no free PSA blocks\n");
    }
    /* Find victim page using FIFO. */
    struct heap_tracker_t *victim=NULL;
    for(int i =0; i<MAXHEAP;i++){
        if(victim==NULL && p->heap_tracker[i].loaded==1){
            victim=&p->heap_tracker[i];
        }
        if((p->heap_tracker[i].last_load_time < victim->last_load_time) && (p->heap_tracker[i].loaded==1)){
            victim= &p->heap_tracker[i];
        }
    }
    /* Print statement. */
    print_evict_page(victim->addr, blockno);
    /* Read memory from the user to kernel memory first. */
    char *kernel_page = kalloc();
    if (!kernel_page)
        printf("kalloc failed during eviction");
    if (copyin(p->pagetable, kernel_page, victim->addr, PGSIZE) < 0) {
        printf("copyin failed in evict_page_to_disk\n");
    }

    //if error check here

    /* Write to the disk blocks. Below is a template as to how this works. There is
     * definitely a better way but this works for now. :p */
    for(int i=0;i<4;i++){
        struct buf* b;
        b = bread(1, PSASTART+i+blockno);
        // Copy page contents to b.data using memmove.
        memmove(b->data, kernel_page + (i * 1024), 1024);
        bwrite(b);
        brelse(b);
    }


    /* Unmap swapped out page */
    uvmunmap(p->pagetable,victim->addr,1,1);

    /* Update the resident heap tracker. */
    p->resident_heap_pages--;
    victim->startblock=blockno;
    victim->loaded=0;

    kfree(kernel_page);
}

/* Retrieve faulted page from disk. */
void retrieve_page_from_disk(struct proc* p, uint64 uvaddr) {
    /* Find where the page is located in disk */
    struct heap_tracker_t *retrieval = NULL;
    for (int i = 0; i < MAXHEAP; i++) {
        if (p->heap_tracker[i].addr == uvaddr) {
            retrieval = &p->heap_tracker[i];
            break;
        }
    }
    /* Print statement. */
    print_retrieve_page(retrieval->addr, retrieval->startblock);

    /* Create a kernel page to read memory temporarily into first. */
    char *kernel_page = kalloc();

    /* Read the disk block into temp kernel page. */
    for(int i=0;i<4;i++){
        struct buf* b;
        b = bread(1, PSASTART+i+retrieval->startblock);
        memmove(kernel_page + (i * 1024), b->data, 1024);
        brelse(b);
    }

    /* Copy from temp kernel page to uvaddr (use copyout) */
    if (copyout(p->pagetable, uvaddr, kernel_page, PGSIZE) < 0)
        printf("retrieve_page_from_disk: copyout failed\n");

    retrieval->loaded=1;
    retrieval->last_load_time=read_current_timestamp();
    retrieval->startblock=-1;

    kfree(kernel_page);

}


void page_fault_handler(void) 
{
    /* Current process struct */
    struct proc *p = myproc();
    struct inode *ip;
    struct elfhdr elf;
    pagetable_t pagetable = 0;
    struct proghdr ph;
    uint64 sz=0;
    struct heap_tracker_t *ht = 0;

    /* Track whether the heap page should be brought back from disk or not. */
    bool load_from_disk = false;

    /* Find faulting address. */
    uint64 faulting_addr = r_stval();
    // printf("Pre shifted faulting addr %p\n",faulting_addr);
    faulting_addr = (faulting_addr >> 12) << 12;
    // printf("Post shifted faulting addr %p\n",faulting_addr);
    print_page_fault(p->name, faulting_addr);

    //if it's copy on write
    if(p->cow_enabled){
        copy_on_write();
        goto out;
    }

    /* Check if the fault address is a heap page. Use p->heap_tracker */
    for(int i=0; i<MAXHEAP;i++){
        if (p->heap_tracker[i].addr==faulting_addr) {
            ht=&p->heap_tracker[i];
            if(ht->startblock>=0){
                load_from_disk=true;
            }
            goto heap_handle;
        }
    }


    /* If it came here, it is a page from the program binary that we must load. */
    // print_load_seg(faulting_addr, 0, 0);

    begin_op();

    if((ip = namei(p->name)) == 0){
        end_op();
        return;
    }
    ilock(ip);

    // Check ELF header
    if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
        goto bad;

    // Load program into memory.
    for(int i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
        if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
        goto bad;
        if(ph.type != ELF_PROG_LOAD)
        continue;
        
        if(faulting_addr>= ph.vaddr && faulting_addr< ph.vaddr+ph.memsz){
            uint64 sz1;
            if((sz1 = uvmalloc(p->pagetable, faulting_addr, faulting_addr + PGSIZE, flags2perm(ph.flags))) == 0){
                goto bad;
            }
            sz = sz1;
            if(loadseg(p->pagetable, faulting_addr, ip, ph.off + (faulting_addr - ph.vaddr), PGSIZE) < 0)
                goto bad;
            print_load_seg(faulting_addr,ph.off, ph.memsz);
            iunlockput(ip);
            end_op();
            goto out;
        }
    }
    /* Go to out, since the remainder of this code is for the heap. */
    iunlockput(ip);
    end_op();
    goto out;

heap_handle:
    /* 2.4: Check if resident pages are more than heap pages. If yes, evict. */
    if (p->resident_heap_pages == MAXRESHEAP) {
        evict_page_to_disk(p);
    }

    /* 2.3: Map a heap page into the process' address space. (Hint: check growproc) */
    if(sz=uvmalloc(p->pagetable,ht->addr,ht->addr+PGSIZE,PTE_W)==0){
        goto bad;
    }

    /* 2.4: Update the last load time for the loaded heap page in p->heap_tracker. */
    ht->loaded = 1;
    ht->last_load_time = read_current_timestamp();

    /* 2.4: Heap page was swapped to disk previously. We must load it from disk. */
    if (load_from_disk) {
        retrieve_page_from_disk(p, faulting_addr);
    }

    /* Track that another heap page has been brought into memory. */
    p->resident_heap_pages++;

out:
    /* Flush stale page table entries. This is important to always do. */
    sfence_vma();
    return;

bad:
    if(pagetable)
        proc_freepagetable(pagetable, sz);
    if(ip){
        iunlockput(ip);
        end_op();
    }
    return ;
}