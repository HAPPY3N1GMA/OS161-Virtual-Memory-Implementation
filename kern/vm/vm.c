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
static void set_entrylo (struct EntryLo *entrylo, int valid, int dirty, uint32_t framenum);
static void copyframe(int from_frame, int to_frame);
static void insert_page(uint32_t index,struct pagetable_entry *page_entry);
static struct pagetable_entry * create_page(struct addrspace *as, uint32_t pagenumber, int dirtybit);
static int readonwrite(struct pagetable_entry *page);
static struct pagetable_entry *create_shared_page(struct addrspace *as, uint32_t pagenumber, uint32_t sharedframe);


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
        /* check address space matches and valid bit */
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
    must hold pagetable lock before calling
*/
static void
insert_page(uint32_t index,struct pagetable_entry *page_entry){
    if(page_entry==NULL){
        return;
    }
    struct pagetable_entry *tmp = pagetable[index];
    page_entry->next = tmp;
    pagetable[index] = page_entry;
}



/*
    create_shared_page
    creates and initialises a new pagetable entry to be read only
*/
static struct pagetable_entry *
create_shared_page(struct addrspace *as, uint32_t pagenumber, uint32_t sharedframe){

    struct pagetable_entry *new = kmalloc(sizeof(struct pagetable_entry));
    if(new==NULL){
        return NULL;
    }
    new->pid = as;
    new->entrylo.uint = 0;
    new->pagenumber = pagenumber;
    new->next = NULL;

    frame_ref_mod(sharedframe, 1);

    /* store shared frame that backs the page */
    set_entrylo (&(new->entrylo.lo), VALID_BIT, INVALID_BIT, sharedframe);

    return new;
}



/*
    create_page
    creates and initialises a new pagetable entry and allocates a frame to back it.
*/
static struct pagetable_entry *
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



static int
readonwrite(struct pagetable_entry *page){

    /* check current frame reference count */
    int from_frame = page->entrylo.lo.framenum;

    /* if last reference to frame  */
    if(frame_ref_cnt(from_frame)==1){
        /* set page as writeable */
        page->entrylo.lo.dirty = 1;
        return 0;
    }

    /* allocate a new frame */
    vaddr_t kvaddr = alloc_kpages(1);
    if(kvaddr==0){
        return ENOMEM;
    }

    /* set the entrylo */
    paddr_t paddr = KVADDR_TO_PADDR(kvaddr);

    /* store frame index for frame that backs the page */
    uint32_t frameindex = paddr >> PADDR_TO_FRAME;
    set_entrylo (&(page->entrylo.lo), VALID_BIT, VALID_BIT, frameindex);

    /* copy old frame contents into new frame contents */
    int to_frame = page->entrylo.lo.framenum;
    copyframe(from_frame, to_frame);

    /* decrement old frame ref count */
    frame_ref_mod(from_frame, -1);

    return 0;
}


/*
    copy_page_table
    given an existing address space, copies all valid page table entries to the new address space,
    and allocates new frames to back the new pages.
*/
int
copy_page_table(struct addrspace *old, struct addrspace *new){

    int npages = pagespace / sizeof(struct pagetable_entry *);

    spinlock_acquire(&pagetable_lock);

    /* walk the length of the pagetable */
    for(int i = 0; i<npages; i++){
        struct pagetable_entry * curr = pagetable[i];

        /* walk the length of the chain */
        while(curr!=NULL){
            /* copy valid pages from old address space */
            if(curr->pid == old && curr->entrylo.lo.valid == 1){
                vaddr_t page_vbase = (curr->pagenumber)<<FRAME_TO_PADDR;
                uint32_t index = hpt_hash(new, page_vbase);

                /* create a new page table entry that shares the same frame */
                struct pagetable_entry *page_entry = create_shared_page(new, curr->pagenumber, curr->entrylo.lo.framenum);

                if(page_entry==NULL){
                    spinlock_release(&pagetable_lock);
                    return ENOMEM;
                }

                /* insert new page table entry*/
                insert_page(index,page_entry);
            }
            curr = curr->next;
        }
    }
    spinlock_release(&pagetable_lock);
    return 0;
}



/*
    copy a physical memory frames contents, from address a to b
*/
static void
copyframe(int from_frame, int to_frame){

    paddr_t from_paddr = from_frame<<FRAME_TO_PADDR;
    paddr_t to_paddr = to_frame<<FRAME_TO_PADDR;

    to_paddr = PADDR_TO_KVADDR(to_paddr);
    from_paddr = PADDR_TO_KVADDR(from_paddr);

    memcpy((void *)to_paddr,(void *)from_paddr,PAGE_SIZE);
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
            struct region_spec *region = as_check_valid_addr(as,faultaddress);
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
            spinlock_acquire(&pagetable_lock);
            insert_page(index,page_entry);
            spinlock_release(&pagetable_lock);

        }else{

            /* Is this a read on write */
            if(faulttype == VM_FAULT_WRITE){

                /* check page is set read only */
                if(page_entry->entrylo.lo.dirty == 0){

                    /* check valid region address. */
                    struct region_spec *region = as_check_valid_addr(as,faultaddress);
                    if(region==NULL){
                        return EFAULT;
                    }

                    /* check region has write permisions */
                    if (!(region->as_perms & PF_W)){
                        return EFAULT;
                    }

                //    panic("WRITEING ON A READ\n");

                    spinlock_acquire(&pagetable_lock);
                    int result = readonwrite(page_entry);
                    spinlock_release(&pagetable_lock);
                    if(result){
                        return result;
                    }

                }
            }
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
static void
set_entrylo (struct EntryLo *entrylo, int valid, int dirty, uint32_t framenum){
    entrylo->unused = 0;
    entrylo->global = 0; //always true for this assignment
    entrylo->valid = valid;
    entrylo->dirty = dirty;
    entrylo->nocache = 0; //not used in this assignment
    entrylo->framenum = framenum;
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
