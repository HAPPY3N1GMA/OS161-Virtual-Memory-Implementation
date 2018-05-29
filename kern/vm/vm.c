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
        //calculate the ram size to get size of frametable
        paddr_t ramtop = ram_getsize();

        int nframes = (ramtop / PAGE_SIZE);
        int framespace = nframes * sizeof(struct frametable_entry);
        //reserve space for frametable
        struct frametable_entry *ft = kmalloc(framespace);
        KASSERT(ft != NULL);
        memset(ft, 0, framespace);

        //reserve space for pagetable
        int npages = (nframes * 2);
        int pagespace = npages * sizeof(struct pagetable_entry);
        pagetable = kmalloc(pagespace);
        KASSERT(pagetable != NULL);
        memset(pagetable, 0, pagespace);


        //reset the base of available memory
        paddr_t freebase = ram_getfirstfree();

        unsigned int i;
        unsigned int bumpallocated = (freebase / PAGE_SIZE);

        if(DEBUGMSG){
            kprintf("bumpallocated: %d and Mem size: %d\n",bumpallocated * PAGE_SIZE, ramtop);
        };

        //set OS, pagetable and frametable frames as unavailable
        for(i = 0; i < bumpallocated; i++){
            ft[i].used = FRAME_USED;
            ft[i].next_free = 0;
        }
        if(DEBUGMSG){
            kprintf("USED Pages frame index: %d\n",i-1);
            kprintf("FIRST free frame index: %d\n",i);
        };
        //set free memory as available and build free list
        firstfreeframe = &(ft[i]);
        unsigned int freeframes = ((ramtop - freebase)/PAGE_SIZE)-1;
        for (; i <  freeframes;i++) {
            ft[i].used = FRAME_UNUSED;
            ft[i].next_free = &(ft[i+1]);
        }

        if(DEBUGMSG){
            kprintf("Last free frame index: %d\n",i);
        };

        //set last page to point to 0
        ft[i].used = FRAME_UNUSED;
        ft[i].next_free = 0;

        //only set frametable to not be zero once all of bumpallocated and our vm is ready
        frametable = ft;

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
