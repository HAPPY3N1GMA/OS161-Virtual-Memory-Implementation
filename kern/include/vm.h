/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
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

#ifndef _VM_H_
#define _VM_H_

/*
 * VM system-related definitions.
 */


#include <machine/vm.h>


/* Fault-type arguments to vm_fault() */
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/

/* frame table defines */
#define INVALID_BIT 0
#define VALID_BIT 1

#define FRAME_USED VALID_BIT
#define FRAME_UNUSED 0

#define PAGE_BITS  12
#define FRAME_TO_PADDR PAGE_BITS
#define PADDR_TO_FRAME FRAME_TO_PADDR


/* Initialization function */
void vm_bootstrap(void);
void frametable_bootstrap(void);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(unsigned npages);
void free_kpages(vaddr_t addr);
int frame_ref_cnt(int index);
void frame_ref_mod(int index, int modifier);

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown(const struct tlbshootdown *);

extern struct frametable_entry *frametable;
extern struct pagetable_entry **pagetable;
extern struct frametable_entry *firstfreeframe;
extern struct spinlock pagetable_lock;

struct EntryLo{
    unsigned int
                        framenum : 20,
                        nocache : 1,
                        dirty : 1,
                        valid : 1,
                        global : 1,
                        unused : 8;
};

struct EntryHi{
    unsigned int pagenum : 20,
                        pid : 6,
                        unused : 6;
};

typedef union {
    struct EntryHi hi;
    struct EntryLo lo;
    uint32_t uint;
} entry_t;


/* Hashed Page Table */
struct pagetable_entry{
    struct addrspace *pid;
    uint32_t pagenumber;
    entry_t entrylo;
    struct pagetable_entry *next;
};

/* VM functions */
int copy_page_table(struct addrspace *old, struct addrspace *new);
uint32_t    hpt_hash(struct addrspace *as, vaddr_t faultaddr);

#endif /* _VM_H_ */
