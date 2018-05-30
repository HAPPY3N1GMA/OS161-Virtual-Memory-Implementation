#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>


/* Place your frametable data-structures here
 * You probably also want to write a frametable initialisation
 * function and call it from vm_bootstrap
 */


static void as_zero_region(paddr_t paddr, unsigned npages);

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

/* Note that this function returns a VIRTUAL address, not a physical
 * address
 * WARNING: this function gets called very early, before
 * vm_bootstrap().  You may wish to modify main.c to call your
 * frame table initialisation function, or check to see if the
 * frame table has been initialised and call ram_stealmem() otherwise.
 */

struct frametable_entry *frametable = 0;

vaddr_t alloc_kpages(unsigned int npages)
{
        paddr_t paddr;


        /* VM System not Initialised - Use Bump Allocator */
        if (frametable == 0) {
            spinlock_acquire(&stealmem_lock);
            paddr = ram_stealmem(npages);
            spinlock_release(&stealmem_lock);
        } else {
            /* VM System Initialised */

            //out of frames return 0
             if(firstfreeframe == 0){
                 return 0;
             }

            //whilst debugging, have this assert to catch weird behaviour
            KASSERT(npages == 1);

            //only allowed to allocate 1 page at a time
            if (npages != 1) {
              return 0;
            }

          // use the allocater
          spinlock_acquire(&stealmem_lock);
          paddr = firstfreeframe - frametable;

          if(DEBUGMSG){
              kprintf("index = %d\n(firstfreeframe - frametable) = %d\nsizeof(struct frametable_entry) = %d\n",(int)paddr,(firstfreeframe - frametable),sizeof(struct frametable_entry));
          };

          paddr <<= FRAME_TO_PADDR;

          firstfreeframe->used = FRAME_USED;
          firstfreeframe = firstfreeframe->next_free;

          spinlock_release(&stealmem_lock);

        }

        if(paddr == 0){
            kprintf("IT WAS ZERO! index = %d\n(firstfreeframe - frametable) = %d\nsizeof(struct frametable_entry) = %d\n",(int)paddr,(firstfreeframe - frametable),sizeof(struct frametable_entry));
            return 0;
        }

        //zero fill the page
        as_zero_region(paddr, npages);

        //kprintf("---------ending alloc_kpages---------\n");

        return PADDR_TO_KVADDR(paddr);
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

void free_kpages(vaddr_t addr)
{
        paddr_t paddr = KVADDR_TO_PADDR(addr);

        KASSERT(paddr != 0);

        int index = paddr >> PADDR_TO_FRAME;

        if(DEBUGMSG){
            kprintf("index: %d\n",index);
        };


        spinlock_acquire(&stealmem_lock);
        frametable[index].next_free = firstfreeframe;
        frametable[index].used = FRAME_UNUSED;
        firstfreeframe = &(frametable[index]);
        spinlock_release(&stealmem_lock);

        if(firstfreeframe == 0){
            kprintf("ERROR THIS SHOULD NEVER HAPPEN!!!! free mem firstfreeframe was set to zero\n");
        }
}
