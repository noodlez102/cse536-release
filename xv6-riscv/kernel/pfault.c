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
    int blockno = 0;
    /* Find victim page using FIFO. */
    /* Print statement. */
    print_evict_page(0, 0);
    /* Read memory from the user to kernel memory first. */
    
    /* Write to the disk blocks. Below is a template as to how this works. There is
     * definitely a better way but this works for now. :p */
    struct buf* b;
    b = bread(1, PSASTART+(blockno));
        // Copy page contents to b.data using memmove.
    bwrite(b);
    brelse(b);

    /* Unmap swapped out page */
    /* Update the resident heap tracker. */
}

/* Retrieve faulted page from disk. */
void retrieve_page_from_disk(struct proc* p, uint64 uvaddr) {
    /* Find where the page is located in disk */

    /* Print statement. */
    print_retrieve_page(0, 0);

    /* Create a kernel page to read memory temporarily into first. */
    
    /* Read the disk block into temp kernel page. */

    /* Copy from temp kernel page to uvaddr (use copyout) */
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

    /* Check if the fault address is a heap page. Use p->heap_tracker */
    for(int i=0; i<MAXHEAP;i++){
        if (p->heap_tracker[i].addr==faulting_addr) {
            ht=&p->heap_tracker[i];
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
    char *mem = kalloc();
    if (mem == 0)
        goto bad;

    memset(mem, 0, PGSIZE);
    if (mappages(p->pagetable, ht->addr, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_U) != 0)
        goto bad;

    ht->loaded = 1;
    ht->last_load_time = ticks;
    /* 2.4: Update the last load time for the   loaded heap page in p->heap_tracker. */

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