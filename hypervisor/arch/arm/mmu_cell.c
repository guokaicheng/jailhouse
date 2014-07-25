/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Copyright (c) ARM Limited, 2014
 *
 * Authors:
 *  Jean-Philippe Brucker <jean-philippe.brucker@arm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <jailhouse/control.h>
#include <jailhouse/paging.h>
#include <jailhouse/printk.h>
#include <asm/sysregs.h>
#include <asm/control.h>

int arch_map_memory_region(struct cell *cell,
			   const struct jailhouse_memory *mem)
{
	u64 phys_start = mem->phys_start;
	u32 flags = PTE_FLAG_VALID | PTE_ACCESS_FLAG;

	if (mem->flags & JAILHOUSE_MEM_READ)
		flags |= S2_PTE_ACCESS_RO;
	if (mem->flags & JAILHOUSE_MEM_WRITE)
		flags |= S2_PTE_ACCESS_WO;
	/*
	 * `DMA' may be a bit misleading here: it is used to define MMIO regions
	 */
	if (mem->flags & JAILHOUSE_MEM_DMA)
		flags |= S2_PTE_FLAG_DEVICE;
	else
		flags |= S2_PTE_FLAG_NORMAL;
	if (mem->flags & JAILHOUSE_MEM_COMM_REGION)
		phys_start = paging_hvirt2phys(&cell->comm_page);
	/*
	if (!(mem->flags & JAILHOUSE_MEM_EXECUTE))
		flags |= S2_PAGE_ACCESS_XN;
	*/

	return paging_create(&cell->arch.mm, phys_start, mem->size,
		mem->virt_start, flags, PAGING_NON_COHERENT);
}

int arch_unmap_memory_region(struct cell *cell,
			     const struct jailhouse_memory *mem)
{
	return paging_destroy(&cell->arch.mm, mem->virt_start, mem->size,
			PAGING_NON_COHERENT);
}

unsigned long arch_paging_gphys2phys(struct per_cpu *cpu_data,
				     unsigned long gphys, unsigned long flags)
{
	/* Translate IPA->PA */
	return paging_virt2phys(&cpu_data->cell->arch.mm, gphys, flags);
}

int arch_mmu_cell_init(struct cell *cell)
{
	cell->arch.mm.root_paging = hv_paging;
	cell->arch.mm.root_table = page_alloc(&mem_pool, 1);
	if (!cell->arch.mm.root_table)
		return -ENOMEM;

	return 0;
}

int arch_mmu_cpu_cell_init(struct per_cpu *cpu_data)
{
	struct cell *cell = cpu_data->cell;
	unsigned long cell_table = paging_hvirt2phys(cell->arch.mm.root_table);
	u64 vttbr = 0;
	u32 vtcr = T0SZ
		| SL0 << TCR_SL0_SHIFT
		| (TCR_RGN_WB_WA << TCR_IRGN0_SHIFT)
		| (TCR_RGN_WB_WA << TCR_ORGN0_SHIFT)
		| (TCR_INNER_SHAREABLE << TCR_SH0_SHIFT)
		| VTCR_RES1;

	if (cell->id > 0xff) {
		panic_printk("No cell ID available\n");
		return -E2BIG;
	}
	vttbr |= (u64)cell->id << VTTBR_VMID_SHIFT;
	vttbr |= (u64)(cell_table & TTBR_MASK);

	arm_write_sysreg(VTTBR_EL2, vttbr);
	arm_write_sysreg(VTCR_EL2, vtcr);

	isb();
	/*
	 * Invalidate all stage-1 and 2 TLB entries for the current VMID
	 * ERET will ensure completion of these ops
	 */
	arm_write_sysreg(TLBIALL, 1);

	return 0;
}