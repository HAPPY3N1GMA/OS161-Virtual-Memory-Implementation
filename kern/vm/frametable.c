#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <elf.h>
#include <proc.h>

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

vaddr_t
alloc_kpages(unsigned int npages)
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





//ummm is this right? because we have a virtual system now...
void
free_kpages(vaddr_t addr)
{
        /* VM System not Initialised */
        if (frametable == 0) {
            // Do nothing -> Leak the memory
            return;
        } else {
        /* VM System Initialised */
        paddr_t paddr = KVADDR_TO_PADDR(addr);
        if(paddr == 0){
            return;
        }

        int index = paddr >> PADDR_TO_FRAME;

        //find the pagetable entry
        struct addrspace *as = proc_getas();

        //lock access to pagetable!
        struct pagetable_entry *hpt_entry = NULL;
        struct pagetable_entry *prev = find_entry_parent(as, addr, &hpt_entry);
        if(hpt_entry!=NULL){
            /* relink pagetable */
            if(prev != NULL){
                prev->next = hpt_entry->next;
            }
            /* free pagetable entry */
            kfree(hpt_entry);
            //unlock access to pagetable
        }

        spinlock_acquire(&stealmem_lock);
        frametable[index].next_free = firstfreeframe;
        frametable[index].used = FRAME_UNUSED;
        firstfreeframe = &(frametable[index]);
        spinlock_release(&stealmem_lock);

        if(firstfreeframe == 0){
            kprintf("ERROR THIS SHOULD NEVER HAPPEN!!!! free mem firstfreeframe was set to zero\n");
        }

    }
}



struct pagetable_entry *
find_entry(struct addrspace *as, vaddr_t vaddr){
    struct pagetable_entry *hpt_entry = NULL;
    find_entry_parent(as, vaddr, &hpt_entry);
    return hpt_entry;
}



/* returns parent and target hpt entries */
struct pagetable_entry *
find_entry_parent(struct addrspace *as, vaddr_t vaddr, struct pagetable_entry **hpt_entry){
    vaddr_t frame = vaddr & PAGE_FRAME; //zeroing out bottom 12 bits (top 4 is frame number, bottom 12 is frameoffset)
    uint32_t index = hpt_hash(as, frame);
    struct pagetable_entry *entry = &(pagetable[index]);
    struct pagetable_entry *parent = NULL;
    /* Look up in page table to see if there is a VALID translation. */
    while(entry!=NULL){
        if(entry->pid == as && entry->entrylo.lo.valid){
            break;
        }
        parent = entry;
        entry = entry->next;
    }
    *hpt_entry = entry;
    return parent;
}



//WE NEED A FILETABLE.H
struct pagetable_entry *
insert_entry(struct addrspace *as, struct pagetable_entry *hpt_entry, struct region_spec *region, uint32_t pagenumber, uint32_t *hi){

    //allocate a new frame
    vaddr_t kvaddr = alloc_kpages(1);

    /* externally chaining the new page table entry */
    if(hpt_entry!=NULL){
        struct pagetable_entry *new = kmalloc(sizeof(struct pagetable_entry));
        if(new==NULL){
            return NULL;
        }
        new->next = hpt_entry->next;
        hpt_entry->next = new;
        hpt_entry = new;
    }else{
        hpt_entry->next = NULL;
    }

    //we update the pagetable entry
    hpt_entry->pid = as;
    hpt_entry->pagenumber = pagenumber;

    /* set the entrylo */
    paddr_t paddr = KVADDR_TO_PADDR(kvaddr);
    uint32_t framenum = paddr >> 12;//faultaddress - region->as_vbase) + region->as_pbase;
    int dirtybit = 0;
    if(region->as_perms & PF_W) dirtybit = 1;
    set_entrylo (&(hpt_entry->entrylo.lo), VALID_BIT, dirtybit, framenum);

    /* set the entryhi */
    entry_t ehi;
    set_entryhi(&(ehi.hi),pagenumber);
    *hi = ehi.uint;

    return hpt_entry;
}
