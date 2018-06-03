#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <current.h>
#include <proc.h>
#include <elf.h>
#include <spl.h>



struct frametable_entry *firstfreeframe = 0;
struct pagetable_entry **pagetable = NULL;

struct spinlock pagetable_lock = SPINLOCK_INITIALIZER;


/* Page table functions */
static struct pagetable_entry *find_page(struct addrspace *as, uint32_t index);


/*
    vm_bootstrap
    initialise Virtual Memory System
*/
void vm_bootstrap(void)
{
        paddr_t ramtop = ram_getsize();
        int nframes = (ramtop / PAGE_SIZE);

        /* reserve space for pagetable */
        int npages = (nframes * 2);
        pagespace = npages * sizeof(struct pagetable_entry *);

        pagetable = kmalloc(pagespace);
        KASSERT(pagetable != NULL);
        for(int i = 0; i<npages; i++) pagetable[i] = NULL;

        /* initialise frametable */
        frametable_bootstrap();
}




/*
    find_page
    looks inside chained page table entry for a VALID translation.
*/
static struct pagetable_entry *
find_page(struct addrspace *as, uint32_t index){
    struct pagetable_entry *curr_entry = pagetable[index];
    while(curr_entry!=NULL){
        //check address space matches and valid bit
        if(curr_entry->pid == as && curr_entry->entrylo.lo.valid){
            break;
        }
        curr_entry = curr_entry->next;
    }
return curr_entry;
}


/*
    insert_page
    inserts a new page entry onto the head of the chain at pagetable index
*/
void
insert_page(uint32_t index,struct pagetable_entry *page_entry){
    KASSERT(page_entry!=NULL);

    spinlock_acquire(&pagetable_lock);

    struct pagetable_entry *tmp = pagetable[index];
    page_entry->next = tmp;
    pagetable[index] = page_entry;

    spinlock_release(&pagetable_lock);
}


/*
    create_page
    creates and initialises a new pagetable entry and allocates a frame to back it.
*/
struct pagetable_entry *
create_page(struct addrspace *as, uint32_t pagenumber, int dirtybit){

    struct pagetable_entry *new = kmalloc(sizeof(struct pagetable_entry));
    if(new==NULL){
        return NULL;
    }
    new->pid = as;
    new->entrylo.uint = 0;
    new->pagenumber = pagenumber;
    new->next = NULL;

    /* allocate a new frame */
    vaddr_t kvaddr = alloc_kpages(1);
    if(kvaddr==0){
        kfree(new);
        return NULL;
    }

    /* set the entrylo */
    paddr_t paddr = KVADDR_TO_PADDR(kvaddr);

    /* store frame index for frame that backs the page */
    uint32_t frameindex = paddr >> PADDR_TO_FRAME;
    set_entrylo (&(new->entrylo.lo), VALID_BIT, dirtybit, frameindex);

    return new;
}



/*
    vm_fault
    handles different faults every time the tlb misses.
    checks that a fault is in a valid region.
    lookup pagetable for an existing entry and load it into the TLB,
    else allocate a new page and frame and load it into the TLB.
*/
int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    uint32_t pagenumber = faultaddress/PAGE_SIZE;
    vaddr_t page_vbase = faultaddress & PAGE_FRAME;
    uint32_t entryhi, entrylo;
    entryhi = entrylo = 0;

    switch (faulttype) {
    	    case VM_FAULT_READONLY:
                return EFAULT;
    	    case VM_FAULT_READ:
    	    case VM_FAULT_WRITE:
    		    break;
    	    default:
    		      return EINVAL;
    	}

        /* No process. This is probably a kernel fault early in boot process*/
        if (curproc == NULL) {
               return EFAULT;
        }

        struct addrspace *as = proc_getas();
        /* No address space set up. */
        if (as == NULL) {
            return EFAULT;
        }

        /* Search for existing page entry*/
        uint32_t index = hpt_hash(as, page_vbase);
        struct pagetable_entry *page_entry = find_page(as, index);

        /* No PageTable Entry Found*/
        if(page_entry==NULL){

            /* Check valid region address. */
            struct region_spec *region = check_valid_address(as,faultaddress);
            if(region==NULL){
                return EFAULT;
            }

            int dirtybit = 0;
            if(region->as_perms & PF_W) dirtybit = 1;

            /* create a new page table entry  */
            page_entry = create_page(as,pagenumber,dirtybit);
            if(page_entry==NULL){
                return ENOMEM;
            }

            /* insert new page table entry */
            insert_page(index,page_entry);
        }

        entryhi = page_vbase;
        entrylo = page_entry->entrylo.uint;

        /* Write to the TLB */
        int spl = splhigh();
        tlb_random(entryhi,entrylo);
        splx(spl);

        return 0;
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

/*
    set_entrylo
    initialise an entrylo struct
*/
void
set_entrylo (struct EntryLo *entrylo, int valid, int dirty, uint32_t framenum){
    entrylo->unused = 0;
    entrylo->global = 0; //always true for this assignment
    entrylo->valid = valid;
    entrylo->dirty = dirty;
    entrylo->nocache = 0; //not used in this assignment
    entrylo->framenum = framenum;
}

/*
    set_entryhi
    initialise an entryhi struct
*/
void
set_entryhi (struct EntryHi *entryhi, uint32_t pagenumber){
    entryhi->pid = 0; //not used for this assignment
    entryhi->pagenum = pagenumber;
}


/*
    hpt_hash
    pagetable hash function
*/
uint32_t
hpt_hash(struct addrspace *as, vaddr_t faultaddr)
{
        uint32_t pagenumber;
        pagenumber = (((uint32_t )as) ^ (faultaddr >> PAGE_BITS)) % (pagespace/sizeof(struct pagetable_entry));
        return pagenumber;
}
