#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>

/* Place your page table functions here */

struct frametable_entry *firstfreeframe = 0;
struct pagetable_entry *pagetable = 0;

void vm_bootstrap(void)
{
        /* Initialise VM sub-system.  You probably want to initialise your
           frame table here as well.
        */

        //calculate the ram size to get size of frametable
        paddr_t ramtop = ram_getsize();

        int framespace = ((ramtop / PAGE_SIZE) * sizeof(struct frametable_entry));

        //reserve space for frametable
        frametable = kmalloc(framespace);
        KASSERT(frametable != NULL);
        memset(frametable, 0, framespace);

        //reserve space for pagetable
        int pagespace = sizeof(struct pagetable_entry)*framespace*2;
        pagetable = kmalloc(pagespace);
        KASSERT(pagetable != NULL);
        memset(pagetable, 0, pagespace);

        //reset the base of available memory
        paddr_t freebase = ram_getfirstfree();

        unsigned int i;
        unsigned int bumpallocated = (freebase / PAGE_SIZE);
        kprintf("bumpallocated: %d and Mem size: %d\n",bumpallocated * PAGE_SIZE, ramtop);
        //set OS, pagetable and frametable frames as unavailable
        for(i = 0; i < bumpallocated; i++){
            frametable[i].used = FRAME_USED;
            frametable[i].next_free = 0;
        }
        kprintf("USED Pages frame index: %d\n",i-1);
        kprintf("FIRST free frame index: %d\n",i);
        //set free memory as available and build free list
        firstfreeframe = &(frametable[i]);
        unsigned int freeframes = ((ramtop - freebase)/PAGE_SIZE)-1;
        for (; i <  freeframes;i++) {
            frametable[i].used = FRAME_UNUSED;
            frametable[i].next_free = &(frametable[i+1]);
        }
        kprintf("Last free frame index: %d\n",i);
        //set last page to point to 0
        frametable[i].used = FRAME_UNUSED;
        frametable[i].next_free = 0;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
        (void) faulttype;
        (void) faultaddress;

        panic("vm_fault hasn't been written yet\n");

        return EFAULT;
}

/*
 *
 * SMP-specific functions.  Unused in our configuration.
 */

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
        (void)ts;
        panic("vm tried to do tlb shootdown?!\n");
}
