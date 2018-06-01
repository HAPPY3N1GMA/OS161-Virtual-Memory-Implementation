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

//temp





/* Place your page table functions here */

struct frametable_entry *firstfreeframe = 0;
struct pagetable_entry *pagetable = 0;


////////////////////////////////////
#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0')
//////////////////



// #define TLBLO_PPAGE   0xfffff000
// #define TLBLO_NOCACHE 0x00000800
// #define TLBLO_DIRTY   0x00000400
// #define TLBLO_VALID   0x00000200

void vm_bootstrap(void)
{
        //
        // uint32_t valid = TLBLO_VALID;
        // kprintf("valid: %d\n",valid);
        //     entry_t test;
        //     test.uint = 0;
        //
        //     kprintf("\nbefore test.uint: %d and valid set: %d\n",test.uint,test.lo.valid);
        //
        //
        //     kprintf("\nshould be zero "BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(test.uint));
        //
        //     test.lo.valid = 1;
        //
        //     kprintf("\ntest.uint: %d and valid set: %d\n",test.uint,test.lo.valid);
        //
        //     kprintf("\nvalid bit should be one "BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(test.uint));
        //     kprintf("\nit should match "BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(valid));
        //
        //
        //     if(test.uint & valid){
        //         panic("IT WAS SET\n ");
        //     }



        //calculate the ram size to get size of frametable
        paddr_t ramtop = ram_getsize();

        int nframes = (ramtop / PAGE_SIZE);
        framespace = nframes * sizeof(struct frametable_entry);
        //reserve space for frametable
        struct frametable_entry *ft = kmalloc(framespace);
        KASSERT(ft != NULL);
        memset(ft, 0, framespace);

        //reserve space for pagetable
        int npages = (nframes * 2);
        pagespace = npages * sizeof(struct pagetable_entry);
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
            kprintf("USED Pages frame pagenumber: %d\n",i-1);
            kprintf("FIRST free frame pagenumber: %d\n",i);
        };
        //set free memory as available and build free list
        firstfreeframe = &(ft[i]);
        unsigned int freeframes = ((ramtop - freebase)/PAGE_SIZE)-1;
        for (; i <  freeframes;i++) {
            ft[i].used = FRAME_UNUSED;
            ft[i].next_free = &(ft[i+1]);
        }

        if(DEBUGMSG){
            kprintf("Last free frame pagenumber: %d\n",i);
        };

        //set last page to point to 0
        ft[i].used = FRAME_UNUSED;
        ft[i].next_free = 0;

        //only set frametable to not be zero once all of bumpallocated and our vm is ready
        frametable = ft;

}



// Called every time the tlb misses (doesn’t find page number)
// Different fault types we have to deal with;
// Vm_fault readonly - tried to write to a ro page (return EFAULT)
// On as_load, need to OR in write bit set to the dirty bit so that it always has write permissions when loading the program
// Other than that, any write attempt on a read only page is as error
// Look up page table to see if there is a VALID translation in it,
// if there is, load it into the TLB
// If not, look up the region in addrspace structure (for each region, check if it falls within base-bound)
// If it is a valid region, allocate a frame (zero’d out), then map to it in the HPT (then put it in the tlb with write random)
// If not valid region, return EFAULT


int
vm_fault(int faulttype, vaddr_t faultaddress)
{
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

        /* Look up page table Hashed index. */
        vaddr_t faultframe = faultaddress & PAGE_FRAME; //zeroing out bottom 12 bits (top 4 is frame number, bottom 12 is frameoffset)
        uint32_t index = hpt_hash(as, faultframe);
        uint32_t pagenumber = faultaddress/PAGE_SIZE;

        struct pagetable_entry *hpt_entry = &(pagetable[index]);
        int spl;
        uint32_t entryhi, entrylo;
        entryhi = entrylo = 0;

        /* Look up in page table to see if there is a VALID translation. */
        while(hpt_entry!=NULL){
            // if there is, load it into the TLB
            if(hpt_entry->pid == as && hpt_entry->entrylo.lo.valid){
                entry_t ehi;
                set_entryhi(&(ehi.hi),pagenumber);
                entryhi = ehi.uint;
                entrylo = hpt_entry->entrylo.uint;
                break;
            }
            hpt_entry = hpt_entry->next;
        }

        /* No PageTable Entry Found -> read in a new page */
        if(hpt_entry==NULL){

            struct region_spec *currregion = as->regions;
            hpt_entry = &(pagetable[index]);

            while(currregion != NULL){

                /* check faultaddr is a valid region */
                if(faultaddress >= currregion->as_vbase &&
                     faultaddress < (currregion->as_vbase + (currregion->as_npages*PAGE_SIZE))){
                         hpt_entry = insert_entry(as, hpt_entry, currregion, pagenumber, &entryhi);
                         if(hpt_entry==NULL){
                             return ENOMEM;
                         }
                          /* get the entrylo and entrylo for TLB Write */
                         entrylo = hpt_entry->entrylo.uint;
                    break;
                }
                currregion = currregion->as_next;
            }

            if(currregion==NULL){
                panic("vm: faultaddress not in any valid region: %d\n",faultaddress);
                return EFAULT;
            }

        }

        if(entryhi==0 || entrylo == 0){
            panic("Serious error here...");
            return EFAULT;
        }

        /* Write to the TLB */
        spl = splhigh();
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


void
set_entrylo (struct EntryLo *entrylo, int valid, int dirty, uint32_t framenum){
    entrylo->unused = 0;
    entrylo->global = 0; //always true for this assignment
    entrylo->valid = valid;
    entrylo->dirty = dirty;
    entrylo->nocache = 0; //not used in this assignment
    entrylo->framenum = framenum;
}

void
set_entryhi (struct EntryHi *entryhi, uint32_t pagenumber){
    entryhi->pid = 0; //not used for this assignment
    entryhi->pagenum = pagenumber;
}
