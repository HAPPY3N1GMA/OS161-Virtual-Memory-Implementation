#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <elf.h>
#include <proc.h>


struct frametable_entry{
    char used;
    struct frametable_entry *next_free;
};


struct frametable_entry *frametable = 0;

struct spinlock frametable_lock = SPINLOCK_INITIALIZER;
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;



void frametable_bootstrap(void){

    //calculate the ram size to get size of frametable
    paddr_t ramtop = ram_getsize();

    //reserve space for frametable
    int nframes = (ramtop / PAGE_SIZE);
    framespace = nframes * sizeof(struct frametable_entry);
    struct frametable_entry *ft = kmalloc(framespace);
    KASSERT(ft != NULL);
    memset(ft, 0, framespace);

    unsigned int i;

    /* reset the base of available memory */
    paddr_t freebase = ram_getfirstfree();
    unsigned int bumpallocated = (freebase / PAGE_SIZE);

    //set OS frames as used
    for(i = 0; i < bumpallocated; i++){
        ft[i].used = FRAME_USED;
        ft[i].next_free = 0;
    }

    /* set free memory as available and build free list */
    firstfreeframe = &(ft[i]);
    unsigned int freeframes = ((ramtop - freebase)/PAGE_SIZE)-1;
    for (; i <  freeframes;i++) {
        ft[i].used = FRAME_UNUSED;
        ft[i].next_free = &(ft[i+1]);
    }

    /* set last page to point to 0 */
    ft[i].used = FRAME_UNUSED;
    ft[i].next_free = 0;

    frametable = ft;
}



 /* Note that this function returns a VIRTUAL address, not a physical
  * address
  * WARNING: this function gets called very early, before
  * vm_bootstrap().  You may wish to modify main.c to call your
  * frame table initialisation function, or check to see if the
  * frame table has been initialised and call ram_stealmem() otherwise.
  */
vaddr_t
alloc_kpages(unsigned int npages)
{
        paddr_t paddr = 0;
        spinlock_acquire(&frametable_lock);

        /* VM System not Initialised - Use Bump Allocator */
        if (frametable == 0) {
            spinlock_acquire(&stealmem_lock);
    	    paddr = ram_stealmem(npages);
    	    spinlock_release(&stealmem_lock);
        } else {

            /* VM System Initialised */
             if(firstfreeframe == 0){
                 spinlock_release(&frametable_lock);
                 return 0;
             }

             if( firstfreeframe->used == FRAME_USED){
                 return 0;
             }

            /* only allocate 1 page at a time */
            if (npages != 1) {
                spinlock_release(&frametable_lock);
                return 0;
            }

            /* Allocate frame entry */
            paddr = firstfreeframe - frametable;
            paddr <<= FRAME_TO_PADDR;
            firstfreeframe->used = FRAME_USED;
            firstfreeframe = firstfreeframe->next_free;
        }

        spinlock_release(&frametable_lock);

        if(paddr == 0){
            return 0;
        }

        /* make sure it's page-aligned */
    	KASSERT((paddr & PAGE_FRAME) == paddr);

        /* zero fill the frame */
         bzero((void *)PADDR_TO_KVADDR(paddr), PAGE_SIZE);

        return PADDR_TO_KVADDR(paddr);
}


void
free_kpages(vaddr_t addr)
{
        /* VM System not Initialised */
        if (frametable == 0) {
            return;
        } else {
        /* VM System Initialised */
        paddr_t paddr = KVADDR_TO_PADDR(addr);
        if(paddr == 0) return;

        /* free the frametable entry */
        int frame_index = paddr >> PADDR_TO_FRAME;

     	if (frametable[frame_index].used != FRAME_USED) {
     		return;
     	}

        spinlock_acquire(&frametable_lock);
        frametable[frame_index].next_free = firstfreeframe;
        frametable[frame_index].used = FRAME_UNUSED;
        firstfreeframe = &(frametable[frame_index]);
        spinlock_release(&frametable_lock);
    }
}
