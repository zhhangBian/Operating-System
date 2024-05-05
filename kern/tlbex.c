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
// 执行tlb重填
void _do_tlb_refill(u_long *pentrylo, u_int virtual_address, u_int asid) {
  // 将远tlb表项无效化
  tlb_invalidate(asid, virtual_address);
  Pte *pte_pointer;
  /*
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
// 处理页写入异常：尝试写入只读页面
void do_tlb_mod(struct Trapframe *tf) {
  // 不能直接使用正常情况下的用户栈的：发生页写入异常的也可能是正常栈的页面
  // 使用**异常处理栈**：栈顶对应的是内存布局中的 UXSTACKTOP
  
  // 保存原先的栈帧
  struct Trapframe former_tf = *tf;

  // 设置栈帧的栈地址为异常处理栈
  // 对于栈指针已经在异常栈中的情况，就不再从异常栈顶开始
  if (tf->regs[29] < USTACKTOP || tf->regs[29] >= UXSTACKTOP) {
    tf->regs[29] = UXSTACKTOP;
  }
  
  // 在用户异常栈底分配一块空间，用于存储trap frame
  // 当前异常处理函数执行的栈
  tf->regs[29] -= sizeof(struct Trapframe);
  // 保存原先的栈帧
  *(struct Trapframe *)tf->regs[29] = former_tf;

  // 如果已经设定了异常处理函数
  if (curenv->env_user_tlb_mod_entry) {
    // 设定a0寄存器的值为原先的栈帧所在的地址，当返回后可以作为返回值被调用
    // 具体逻辑见entry.S
    tf->regs[4] = tf->regs[29];
    // 留出第一个参数的空间
    tf->regs[29] -= sizeof(tf->regs[4]);
    // 取出进程的处理函数，跳转回epc后执行处理函数
    tf->cp0_epc = curenv->env_user_tlb_mod_entry;
  } else {
    panic("TLB Mod but no user handler registered");
  }
}
#endif
