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


static void copyframe(struct pagetable_entry *from, struct pagetable_entry *to);


/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
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

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
    // spinlock_acquire(&pagetable_lock);
    // spinlock_acquire(&frametable_lock);
    *ret = NULL;

    if(old==NULL){
        return 0;
    }

    struct addrspace *new;
    new = as_create();
    if (new==NULL) {
            return ENOMEM;
    }

    struct region_spec *curr_region = old->regions;

    /* copy all regions from old into new */
    while(curr_region!=NULL){
        int r = curr_region->as_perms & PF_R;
        int w = curr_region->as_perms & PF_W;
        int x = curr_region->as_perms & PF_X;
        int result = as_define_region(new, curr_region->as_vbase , curr_region->as_npages * PAGE_SIZE, r, w, x);
        if(result){
            as_destroy(new);
            return result;
        }

        //copyin new page frames --> advanced will share page frames instead

        /* Look up page frames in this region*/
        vaddr_t faultaddr = curr_region->as_vbase & PAGE_FRAME;

        uint32_t old_index = hpt_hash(old, faultaddr);

        struct pagetable_entry *curr_hpt = &(pagetable[old_index]);
        struct pagetable_entry *prev_hpt = NULL;


        struct pagetable_entry *new_chain = NULL;

        while(curr_hpt!=NULL){
            /* copy all relevant frames in this region*/
            if(old == curr_hpt->pid){
                struct pagetable_entry *new_entry = kmalloc(sizeof(struct pagetable_entry));
                if(new_entry==NULL){
                    //free allready newly chained entries
                    return ENOMEM;
                }

                /* initialise pagetable entry contents */
                init_entry(new, new_entry, curr_region, curr_hpt->pagenumber);

                /* link into new chain */
                if(new_chain==NULL){
                    new_chain = new_entry;
                }else{
                    new_entry->next = new_chain;
                    new_chain = new_entry;
                }

                /* copy frame contents */
                copyframe(curr_hpt, new_entry);
            }

            prev_hpt = curr_hpt;
            curr_hpt = curr_hpt->next;
        }

        /* link new chain onto end of existing chain */
        if(prev_hpt != NULL){
            prev_hpt->next = new_chain;
        }

        curr_region = curr_region->as_next;
    }
//panic("AS COPY WAS CALLED\n");
    *ret = new;
    return 0;
}


/*
    copy frame from a to b
*/
static void
copyframe(struct pagetable_entry *from, struct pagetable_entry *to){
    int from_frame = from->entrylo.lo.framenum;
    paddr_t from_paddr = from_frame<<FRAME_TO_PADDR;

    int to_frame = to->entrylo.lo.framenum;
    paddr_t to_paddr = to_frame<<FRAME_TO_PADDR;

    to_paddr = PADDR_TO_KVADDR(to_paddr);
    from_paddr = PADDR_TO_KVADDR(from_paddr);

    memcpy((void *)to_paddr,(void *)from_paddr,PAGE_SIZE);
}




void
as_destroy(struct addrspace *as)
{

    /* free all pages and frames */

    int npages = pagespace / sizeof(struct pagetable_entry);

    for (int i = 0; i < npages; i++) {
        struct pagetable_entry *curr_page = &pagetable[i];
        struct pagetable_entry *next_page = NULL;
        while(curr_page!=NULL){
                next_page = curr_page->next;
                /* free each page and frame */
                if(curr_page->pid == as){
                    paddr_t framenum = (curr_page->entrylo.lo.framenum)<<FRAME_TO_PADDR;
                    free_kpages(PADDR_TO_KVADDR(framenum));
                }
                curr_page = next_page;
        }
    }

    /* free all regions */
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

void
as_deactivate(void)
{
        /* Flush TLB */
        as_activate();
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
                 int readable, int writeable, int executable)
{
        if(as==NULL){
            return 0; //fix this error code
        }

        size_t npages;

        /* Align the region. First, the base... */
        memsize += vaddr & ~(vaddr_t)PAGE_FRAME; //add offset onto memsize
        vaddr &= PAGE_FRAME; //chop the offset off the virtual address

        /* ...and now the length. */
        memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME; //ceiling memsize to nearest pagesize

        npages = memsize / PAGE_SIZE;

        struct region_spec *region = kmalloc(sizeof(struct region_spec));
        if(region==NULL){
            return ENOMEM;
        }

        if(readable) region->as_perms |= PF_R;
        if(writeable) region->as_perms |= PF_W;
        if(executable) region->as_perms |= PF_X;

        region->as_vbase = vaddr;
        region->as_pbase = KVADDR_TO_PADDR(vaddr);
        region->as_npages = npages;
        region->as_next = as->regions;
        as->regions = region;

        return 0;
}

int
as_prepare_load(struct addrspace *as)
{
        struct region_spec *curr_region = as->regions;

        //set all regions as read write -> temp until extended assignment
        while(curr_region!=NULL){
            if(!(curr_region->as_perms & PF_W)){
                //modified flag so that we can revert the wrtie flag after load
                curr_region->as_perms |= (PF_W | OS_M );
            }
            curr_region = curr_region->as_next;
        }
        return 0;
}


/*
- Re-set the permissions in the addrspace struct to what they were before

- Go through the page table and unset the (D)irty bits on the page table entries for any pages that were loaded into (now) read-only regions

- You also need to remove these pages from the TLB (you can just flush the whole TLB).
*/
int
as_complete_load(struct addrspace *as)
{
    struct region_spec *curr_region = as->regions;
    while(curr_region!=NULL){
        /* remove modified and writeable flags from modified regions */
        if(curr_region->as_perms & OS_M){
            curr_region->as_perms &= ~(PF_W | OS_M );

            // https://piazza.com/class/jdwg14qxhhb4kp?cid=536

            /* Look up page frames in this region*/
            vaddr_t frame = curr_region->as_vbase & PAGE_FRAME;
            uint32_t index = hpt_hash(as, frame);
            struct pagetable_entry *hpt_entry = &(pagetable[index]);

            /* Set all entries dirty bit to read only */
            while(hpt_entry!=NULL){
                hpt_entry->entrylo.lo.dirty = 0;
                hpt_entry = hpt_entry->next;
            }

        }
        curr_region = curr_region->as_next;
    }

    /* Flush TLB */
    as_activate();

    return 0;
}



int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
    vaddr_t stackbase = USERSTACK - STACKSIZE;

    int result = as_define_region(as, stackbase, STACKSIZE, VALID_BIT, VALID_BIT, INVALID_BIT);
    if(result){
        return result;
    }

    /* Initial user-level stack pointer */
    *stackptr = USERSTACK;

    return 0;
}


uint32_t
hpt_hash(struct addrspace *as, vaddr_t faultaddr)
{
        uint32_t pagenumber;

        pagenumber = (((uint32_t )as) ^ (faultaddr >> PAGE_BITS)) % (pagespace/sizeof(struct pagetable_entry));
        return pagenumber;
}
