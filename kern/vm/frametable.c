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

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

/* Note that this function returns a VIRTUAL address, not a physical
 * address
 * WARNING: this function gets called very early, before
 * vm_bootstrap().  You may wish to modify main.c to call your
 * frame table initialisation function, or check to see if the
 * frame table has been initialised and call ram_stealmem() otherwise.
 */

struct frametable_entry *frametable = 0; // use 0 if it doesn't work

vaddr_t alloc_kpages(unsigned int npages)
{
        /*
         * IMPLEMENT ME.  You should replace this code with a proper
         *                implementation.
         */

        // whilst debugging, have this assert to catch weird behaviour
        KASSERT(npages == 1);

        // only allowed to allocate 1 page at a time
        if (npages != 1) {
          return NULL;
        }

        if (frametable == 0) { // vm sys not initialised

          paddr_t paddr = ram_stealmem(npages);

        } else { // vm sys initialised

          // use the allocater


        }


        /* OLD IMPLEMENTATION
        paddr_t addr;

        spinlock_acquire(&stealmem_lock);
        addr = ram_stealmem(npages);
        spinlock_release(&stealmem_lock);

        if(addr == 0)
                return 0;

        return PADDR_TO_KVADDR(addr);
        */
}

void free_kpages(vaddr_t addr)
{
        (void) addr;
}
