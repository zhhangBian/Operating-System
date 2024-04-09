#include <bitops.h>
#include <env.h>
#include <pmap.h>

/* Lab 2 Key Code "tlb_invalidate" */
/* Overview:
 *   Invalidate the TLB entry with specified 'asid' and virtual address 'va'.
 *
 * Hint:
 *   Construct a new Entry HI and call 'tlb_out' to flush TLB.
 *   'tlb_out' is defined in mm/tlb_asm.S
 */
// 更新页表后将TLB对应的项无效，保证虚拟-物理地址映射的正确，下次访问时触发TLB重填
void tlb_invalidate(u_int asid, u_long virtual_address) {
  // 调用相应的汇编函数
	tlb_out((virtual_address & ~GENMASK(PGSHIFT, 0)) | (asid & (NASID - 1)));
}
/* End of Key Code "tlb_invalidate" */

static void passive_alloc(u_int va, Pde *pgdir, u_int asid) {
	struct Page *p = NULL;

	if (va < UTEMP) {
		panic("address too low");
	}

	if (va >= USTACKTOP && va < USTACKTOP + PAGE_SIZE) {
		panic("invalid memory");
	}

	if (va >= UENVS && va < UPAGES) {
		panic("envs zone");
	}

	if (va >= UPAGES && va < UVPT) {
		panic("pages zone");
	}

	if (va >= ULIM) {
		panic("kernel address");
	}

	panic_on(page_alloc(&p));
	panic_on(page_insert(pgdir, asid, p, PTE_ADDR(va), (va >= UVPT && va < ULIM) ? 0 : PTE_D));
}

/* Overview:
 *  Refill TLB.
 */
void _do_tlb_refill(u_long *pentrylo, u_int virtual_address, u_int asid) {
	tlb_invalidate(asid, virtual_address);
	Pte *pte_pointer;
	/* Hints:
	 *  Invoke 'page_lookup' repeatedly in a loop to find the page table entry '*pte_pointer'
	 * associated with the virtual address 'va' in the current address space 'cur_pgdir'.
	 *
	 *  **While** 'page_lookup' returns 'NULL', indicating that the '*pte_pointer' could not be found,
	 *  allocate a new page using 'passive_alloc' until 'page_lookup' succeeds.
   * 
   * 尝试在循环中调用'page_lookup'以查找虚拟地址va在当前进程页表中对应的页表项'*pte_pointer'
   * 如果'page_lookup'返回'NULL'，表明找不到对应页表，使用'passive_alloc'为va 所在的虚拟页面分配物理页面，
   * 你可以在调用函数时，使用全局变量cur_pgdir 作为其中一个实参。
	 */

	// cur_pgdir 存储了当前进程一级页表基地址位于kseg0 的虚拟地址
  while (page_lookup(cur_pgdir, virtual_address, &pte_pointer) == NULL) {
		passive_alloc(virtual_address, cur_pgdir, asid);
	}

	pte_pointer = (Pte *)((u_long)pte_pointer & ~0x7);
	pentrylo[0] = pte_pointer[0] >> 6;
	pentrylo[1] = pte_pointer[1] >> 6;
}

#if !defined(LAB) || LAB >= 4
/* Overview:
 *   This is the TLB Mod exception handler in kernel.
 *   Our kernel allows user programs to handle TLB Mod exception in user mode, so we copy its
 *   context 'tf' into UXSTACK and modify the EPC to the registered user exception entry.
 *
 * Hints:
 *   'env_user_tlb_mod_entry' is the user space entry registered using
 *   'sys_set_user_tlb_mod_entry'.
 *
 *   The user entry should handle this TLB Mod exception and restore the context.
 */
void do_tlb_mod(struct Trapframe *tf) {
	struct Trapframe tmp_tf = *tf;

	if (tf->regs[29] < USTACKTOP || tf->regs[29] >= UXSTACKTOP) {
		tf->regs[29] = UXSTACKTOP;
	}
	tf->regs[29] -= sizeof(struct Trapframe);
	*(struct Trapframe *)tf->regs[29] = tmp_tf;
	Pte *pte;
	page_lookup(cur_pgdir, tf->cp0_badvaddr, &pte);
	if (curenv->env_user_tlb_mod_entry) {
		tf->regs[4] = tf->regs[29];
		tf->regs[29] -= sizeof(tf->regs[4]);
		// Hint: Set 'cp0_epc' in the context 'tf' to 'curenv->env_user_tlb_mod_entry'.
		/* Exercise 4.11: Your code here. */

	} else {
		panic("TLB Mod but no user handler registered");
	}
}
#endif
