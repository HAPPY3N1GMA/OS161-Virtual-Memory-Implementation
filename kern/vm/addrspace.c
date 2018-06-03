/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *        The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <elf.h>



/*
    as_check_valid_addr
    Checks a given address against an address space regions.
    If valid region found, returns the region.
*/
struct region_spec *
as_check_valid_addr(struct addrspace *as, vaddr_t addr){
    if(as==NULL){
        return NULL;
    }
    struct region_spec *currregion = as->regions;
    while(currregion != NULL){
        /* check addr is a valid region */
        if(addr >= currregion->as_vbase &&
             addr < (currregion->as_vbase + (currregion->as_npages*PAGE_SIZE))){
             return currregion;
        }
        currregion = currregion->as_next;
    }
    return NULL;
}




/*
    as_create
    creates new addresspace struct
*/
struct addrspace *
as_create(void)
{
        struct addrspace *as;
        as = kmalloc(sizeof(struct addrspace));
        if (as == NULL) {
                return NULL;
        }
        as->regions = NULL;
        return as;
}


/*
    as_copy_region
    create a new address space region that is a copy of the given region
*/
static int
as_copy_region(struct addrspace *as, struct region_spec *region){
    if(as==NULL){
        return EFAULT;
    }

    if(region==NULL){
        return EFAULT;
    }

    int r = region->as_perms & PF_R;
    int w = region->as_perms & PF_W;
    int x = region->as_perms & PF_X;
    return as_define_region(as, region->as_vbase , region->as_npages * PAGE_SIZE, r, w, x);
}


/*
    as_copy
    deep copy an address space and its pagetable entries
*/
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
    *ret = NULL;
    int result = 0;

    if(old==NULL){
        return EFAULT;
    }

    struct addrspace *new_as = as_create();
    if (new_as==NULL) {
            return ENOMEM;
    }

    /* No lock required on addrspace as regions are never changed */
    struct region_spec *curr_region = old->regions;
    while(curr_region!=NULL){
        /* copy old regions into the new address space*/
        result = as_copy_region(new_as,curr_region);
        if(result){
            as_destroy(new_as);
            return result;
        }
        curr_region = curr_region->as_next;
    }

    /* allocate new page table entries*/
    result = copy_page_table(old, new_as);
    if(result){
        as_destroy(new_as);
        return result;
    }

    *ret = new_as;
    return 0;
}


/*
    as_destroy
    free an address space and all its pages and frames
*/
void
as_destroy(struct addrspace *as)
{
    if(as == NULL){
        return;
    }

    /* free all pages and frames */
    int npages = pagespace / sizeof(struct pagetable_entry);

    spinlock_acquire(&pagetable_lock);

    /*walk pagetable length */
    for (int i = 0; i < npages; i++) {
        struct pagetable_entry *curr_page = pagetable[i];
        struct pagetable_entry *next_page = NULL;
        struct pagetable_entry *prev_page = NULL;
        struct pagetable_entry *head = pagetable[i];

        /*walk chain length */
        while(curr_page!=NULL){
                next_page = curr_page->next;

                if(curr_page->pid == as){

                    /* free frame */
                    paddr_t framebase = (curr_page->entrylo.lo.framenum)<<FRAME_TO_PADDR;
                    free_kpages(PADDR_TO_KVADDR(framebase));

                    /* update head of chain */
                    if(curr_page==head){
                         pagetable[i] = next_page;
                         head = next_page;
                    }else{
                        prev_page->next = next_page;
                    }
                    kfree(curr_page);
                }else{
                    prev_page = curr_page;
                }
                curr_page = next_page;
        }
    }

    spinlock_release(&pagetable_lock);

    /* free all regions - no lock required*/
    struct region_spec *curr_region = as->regions;
    struct region_spec *next_region = NULL;

    while(curr_region!=NULL){
        next_region = curr_region->as_next;
        kfree(curr_region);
        curr_region = next_region;
    }

    kfree(as);

    /* Flush TLB */
    as_activate();
}


/*
    as_activate
    from dumbvm -> flush tlb
*/
void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

    /* Flush TLB */
	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

/*
    as_deactivate
    flush tlb
*/
void
as_deactivate(void)
{
        /* Flush TLB */
        as_activate();
}


 /*
     as_define_region
     Set up a segment at virtual address VADDR of size MEMSIZE.
     The segment in memory extends from VADDR up to (but not including)
     VADDR+MEMSIZE.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
                 int readable, int writeable, int executable)
{

        if(as==NULL){
            return EFAULT;
        }

        /* Align the region. First, the base... */
        memsize += vaddr & ~(vaddr_t)PAGE_FRAME; //add offset onto memsize
        vaddr &= PAGE_FRAME; //chop the offset off the virtual address

        /* ...and now the length. */
        memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

        /* initialise region */
        struct region_spec *region = kmalloc(sizeof(struct region_spec));
        if(region==NULL){
            return ENOMEM;
        }

        if(readable) region->as_perms |= PF_R;
        if(writeable) region->as_perms |= PF_W;
        if(executable) region->as_perms |= PF_X;

        region->as_vbase = vaddr;
        region->as_npages = memsize / PAGE_SIZE;
        region->as_next = as->regions;
        as->regions = region;

        return 0;
}


/*
    as_prepare_load
    Set all regions as temporarily writeable so OS can load into memory
*/
int
as_prepare_load(struct addrspace *as)
{
        if(as==NULL){
            return EFAULT;
        }

        struct region_spec *curr_region = as->regions;
        while(curr_region!=NULL){
            if(!(curr_region->as_perms & PF_W)){
                /* insert write and modified flag so that we can revert after load */
                curr_region->as_perms |= (PF_W | OS_M );
            }
            curr_region = curr_region->as_next;
        }
        return 0;
}



/*
    as_complete_load
    Reset all the pagetable entries that were modified to be writeable on load,
    reset regions permissions that were modified and flush the TLB.
*/
int
as_complete_load(struct addrspace *as)
{
    int npages = pagespace / sizeof(struct pagetable_entry *);

    if(as==NULL){
        return EFAULT;
    }

    spinlock_acquire(&pagetable_lock);

    /* walk the length of the pagetable */
    for(int i = 0; i<npages; i++){
        struct pagetable_entry * curr = pagetable[i];

        /* walk the length of the chain */
        while(curr!=NULL){
            /* remove tagged pagetable entries writeable flags */
            if(curr->pid == as && curr->entrylo.lo.valid == 1){
                //check valid region
                vaddr_t page_vbase = (curr->pagenumber)<<PAGE_BITS;
                struct region_spec * region =as_check_valid_addr(as, page_vbase);
                if(region==NULL){
                    spinlock_release(&pagetable_lock);
                    return EFAULT;
                }
                /* turn off dirty bit */
                if(region->as_perms & OS_M){
                    curr->entrylo.lo.dirty = 0;
                }
            }
            curr = curr->next;
        }

        /* set all modified regions to correct perms - no lock required*/
        struct region_spec * curr_region = as->regions;
        while(curr_region!=NULL){
            if(curr_region->as_perms & OS_M){
                curr_region->as_perms &= ~(PF_W | OS_M );
            }
            curr_region = curr_region->as_next;
        }
    }

    spinlock_release(&pagetable_lock);

    /* Flush TLB */
    as_activate();

    return 0;
}


/*
    as_define_stack
    setup a fixed-size stack region and return the stack pointer.
*/
int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
    if(as==NULL){
        return EFAULT;
    }

    if(stackptr==NULL){
        return EFAULT;
    }

    vaddr_t stackbase = USERSTACK - STACKSIZE;

    int result = as_define_region(as, stackbase, STACKSIZE, VALID_BIT, VALID_BIT, INVALID_BIT);
    if(result){
        return result;
    }

    /* Initial user-level stack pointer */
    *stackptr = USERSTACK;

    return 0;
}
