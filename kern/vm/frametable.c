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
    int ref;
    struct frametable_entry *next_free;
};

struct frametable_entry *frametable = 0;
struct spinlock frametable_lock = SPINLOCK_INITIALIZER;


/*
    frametable_bootstrap
    initialise frametable and reset available memory base.
    set all OS memory frames as used, and link all free frames
*/
void frametable_bootstrap(void){
    unsigned int i;

    /* calculate the ram size to get size of frametable */
    paddr_t ramtop = ram_getsize();

    /* reserve space for frametable */
    int nframes = (ramtop / PAGE_SIZE);
    framespace = nframes * sizeof(struct frametable_entry);
    struct frametable_entry *ft = kmalloc(framespace);
    KASSERT(ft != NULL);
    memset(ft, 0, framespace);

    /* reset the base of available memory */
    paddr_t freebase = ram_getfirstfree();
    unsigned int bumpallocated = (freebase / PAGE_SIZE);

    /* set OS frames as used */
    for(i = 0; i < bumpallocated; i++){
        ft[i].used = FRAME_USED;
        ft[i].ref = 1;
        ft[i].next_free = 0;
    }

    /* set free memory as available and build free list */
    firstfreeframe = &(ft[i]);
    unsigned int freeframes = ((ramtop - freebase)/PAGE_SIZE)-1;
    for (; i <  freeframes;i++) {
        ft[i].used = FRAME_UNUSED;
        ft[i].ref = 0;
        ft[i].next_free = &(ft[i+1]);
    }

    /* set last page to point to 0 */
    ft[i].used = FRAME_UNUSED;
    ft[i].ref = 0;
    ft[i].next_free = 0;

    frametable = ft;
}



/*
    alloc_kpages
    allocate next free physical frame and zero out its region.
*/
vaddr_t
alloc_kpages(unsigned int npages)
{
        paddr_t paddr = 0;
        spinlock_acquire(&frametable_lock);

        /* VM System not Initialised - Use Bump Allocator */
        if (frametable == 0) {
    	    paddr = ram_stealmem(npages);
            spinlock_release(&frametable_lock);
        } else {

            /* VM System Initialised */
             if(firstfreeframe == 0){
                 spinlock_release(&frametable_lock);
                 return 0;
             }

             /* check frame is available */
             if(firstfreeframe->used == FRAME_USED){
                 spinlock_release(&frametable_lock);
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
            firstfreeframe->ref = 1;
            firstfreeframe = firstfreeframe->next_free;
            spinlock_release(&frametable_lock);
        }

        /* ensure valid address */
        if(paddr == 0){
            return 0;
        }

        /* ensure it's page-aligned */
    	if((paddr & PAGE_FRAME) != paddr){
                return 0;
        }

        /* zero fill the frame */
         bzero((void *)PADDR_TO_KVADDR(paddr), PAGE_SIZE);

        return PADDR_TO_KVADDR(paddr);
}



/*
    free_kpages
    free frametable entry and make available for allocation
*/
void
free_kpages(vaddr_t addr)
{
        /* VM System not Initialised */
        if (frametable == 0) {
            /* bump allocated leak */
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
        /* decrement reference counter */
        frametable[frame_index].ref -= 1;
        /* ensure last page looking at this frame, then free the frame */
        if(frametable[frame_index].ref <= 0){
            frametable[frame_index].next_free = firstfreeframe;
            frametable[frame_index].used = FRAME_UNUSED;
            firstfreeframe = &(frametable[frame_index]);
        }
        spinlock_release(&frametable_lock);
    }
}

/*
    frame_ref_cnt
    return frame reference counter value
*/
int
frame_ref_cnt(int index){
    return frametable[index].ref;
}


/*
    frame_ref_mod
    modify frame reference counter
*/
void
frame_ref_mod(int index, int modifier){
    spinlock_acquire(&frametable_lock);
    frametable[index].ref += modifier;
    spinlock_release(&frametable_lock);
}
