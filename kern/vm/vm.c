#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>

#define FT_ENTRY_SIZE 4
/* Place your page table functions here */


void vm_bootstrap(void)
{
        /* Initialise VM sub-system.  You probably want to initialise your
           frame table here as well.
        */

        // each frametable_entry is 4 bytes (32 bits)

        paddr_t ft_base = MIPS_RAMTOP - ((MIPS_RAMTOP / PAGE_SIZE) * FT_ENTRY_SIZE);
        frametable = (struct frame_table_entry *)ft_base;


}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
        (void) faulttype;
        (void) faultaddress;

        panic("vm_fault hasn't been written yet\n");

        return EFAULT;
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
