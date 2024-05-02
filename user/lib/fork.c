#include <env.h>
#include <lib.h>
#include <mmu.h>

/* Overview:
 *   Map the faulting page to a private writable copy.
 *
 * Pre-Condition:
 * 	'va' is the address which led to the TLB Mod exception.
 *
 * Post-Condition:
 *  - Launch a 'user_panic' if 'va' is not a copy-on-write page.
 *  - Otherwise, this handler should map a private writable copy of
 *    the faulting page at the same address.
 */
// 是写时复制的异常处理函数
static void __attribute__((noreturn)) cow_entry(struct Trapframe *tf) {
  u_int virtual_address = tf->cp0_badvaddr;
  u_int permission;

  // 获取页面的权限
  permission = vpt[VPN(virtual_address)] & 0xfff;
  // 判断权限是否为写时复制
  if (!(permission & PTE_COW)) {
    user_panic("perm doesn't have PTE_COW");
  }
  // 移除PTE_COW权限，设置为PTE_D可写权限
  permission = (permission & ~PTE_COW) | PTE_D;

  // 在UCOW获取新物理页面，分配内存
  // 因为处于用户程序中，所以不能使用page_alloc，而需要使用系统调用
  syscall_mem_alloc(0, (void *)UCOW, permission);

  // 拷贝数据
  // va是随意的地址，不一定对其，要手动对其
  memcpy((void *)UCOW, (void *)ROUNDDOWN(virtual_address, PAGE_SIZE), PAGE_SIZE);

  // 将发生页写入异常的地址va 映射到临时页面上，并设置相关权限
  syscall_mem_map(0, (void *)UCOW, 0, (void *)virtual_address, permission);

  // 解除临时地址UCOW 的内存映射
  syscall_mem_unmap(0, (void *)UCOW);

  // Step 7: Return to the faulting routine.
  int func_info = syscall_set_trapframe(0, tf);
  user_panic("syscall_set_trapframe returned %d", func_info);
}

/* Overview:
 *   Grant our child 'envid' access to the virtual page 'vpn' (with address 'vpn' * 'PAGE_SIZE') in
 * our (current env's) address space. 'PTE_COW' should be used to isolate the modifications on
 * unshared memory from a parent and its children.
 *
 * Post-Condition:
 *   If the virtual page 'vpn' has 'PTE_D' and doesn't has 'PTE_LIBRARY', both our original virtual
 *   page and 'envid''s newly-mapped virtual page should be marked 'PTE_COW' and without 'PTE_D',
 *   while the other permission bits are kept.
 *
 *   If not, the newly-mapped virtual page in 'envid' should have the exact same permission as our
 *   original virtual page.
 *
 * Hint:
 *   - 'PTE_LIBRARY' indicates that the page should be shared among a parent and its children.
 *   - A page with 'PTE_LIBRARY' may have 'PTE_D' at the same time, you should handle it correctly.
 *   - You can pass '0' as an 'envid' in arguments of 'syscall_*' to indicate current env because
 *     kernel 'envid2env' converts '0' to 'curenv').
 *   - You should use 'syscall_mem_map', the user space wrapper around 'msyscall' to invoke
 *     'sys_mem_map' in kernel.
 */
// 设置子进程的地址空间
// 将地址空间中需要与子进程共享的页面映射给子进程，需要遍历父进程的大部分用户空间页
// - 只读页面：对于不具有PTE_D权限位的页面，按照相同权限（只读）映射给子进程即可。
// - 写时复制页面：即具有PTE_COW权限位的页面。
//   这类页面是之前的fork时duppage的结果，且在本次fork前必然未被写入过。
// - 共享页面：即具有PTE_LIBRARY权限位的页面。
//   这类页面需要保持共享可写的状态，即在父子进程中映射到相同的物理页，使对其进行修改的结果相互可见。
// - 可写页面：即具有PTE_D权限位，且不符合以上特殊情况的页面。
//   这类页面需要在父进程和子进程的页表项中都使用PTE_COW 权限位进行保护。
static void duppage(u_int envid, u_int page_number) {
  u_int page_address;
  u_int permission;

  // 从页控制块编号获取地址
  page_address = page_number << PGSHIFT;
  permission = vpt[page_number] & 0xfff;

  /* Step 2: If the page is writable, and not shared with children, and not marked as COW yet,
   * then map it as copy-on-write, both in the parent (0) and the child (envid). */
  /* Hint: The page should be first mapped to the child before remapped in the parent. (Why?)
   */
  u_int if_set_cow = 0;
  // 如果页面可写，并且不与子进程共享，且还未标记为PTE_COW，则在父子进程中同时设置为写时复制
  if ((permission & PTE_D) && !(permission & PTE_LIBRARY)) {
    permission = (permission & ~ PTE_D) | PTE_COW;
    if_set_cow = 1;
  }

  //
  syscall_mem_map(0, page_address, envid, page_address, permission);

  if (if_set_cow) {
    syscall_mem_map(0, page_address, 0, page_address, permission);
  }
}

/* Overview:
 *   User-level 'fork'. Create a child and then copy our address space.
 *   Set up ours and its TLB Mod user exception entry to 'cow_entry'.
 *
 * Post-Conditon:
 *   Child's 'env' is properly set.
 *
 * Hint:
 *   Use global symbols 'env', 'vpt' and 'vpd'.
 *   Use 'syscall_set_tlb_mod_entry', 'syscall_getenvid', 'syscall_exofork',  and 'duppage'.
 */
// fork()是一个用户态函数！
// 在此，进程进行分叉，原先的行为逻辑一分为二，开始进行独立的运行
int fork(void) {
  u_int child;
  u_int i;

  /* Step 1: Set our TLB Mod user exception entry to 'cow_entry' if not done yet. */
  if (env->env_user_tlb_mod_entry != (u_int)cow_entry) {
    try(syscall_set_tlb_mod_entry(0, cow_entry));
  }

  /* Step 2: Create a child env that's not ready to be scheduled. */
  // Hint: 'env' should always point to the current env itself, so we should fix it to the
  // correct value.
  child = syscall_exofork();
  if (child == 0) {
    env = envs + ENVX(syscall_getenvid());
    return 0;
  }

  // 父子进程虽然共享了页表，但页表项还没有设置PTE_COW位，需要通过 duppage 函数设置
  // 如果当前页表项具有PTE_D 权限，且不是共享页面 PTE_LIBRARY，则需要重新设置页表项的权限。
  // duppage会对每一个页表项进行操作，因此我们需要在 fork 中遍历所有的页表项。
  // 只遍历 USTACKTOP 之下的地址空间，因为**其上的空间总是会被共享**。
  for (i = 0; i < VPN(USTACKTOP); i++) {
    // 在调用 duppage 之前，我们判断页目录项和页表项是否有效。
    // 如果不判断则会在 duppage 函数中发生异常（最终是由 page_lookup 产生的）。
    // 需要注意取页目录项的方法，vpd是页目录项数组，i相当于地址的高20位，需要取得地址的高10位作为页目录的索引
    if ((vpd[i >> 10] & PTE_V) && (vpt[i] & PTE_V)) {
      duppage(child, i);
    }
  }

  // 设置子进程的写时复制异常处理函数
  try(syscall_set_tlb_mod_entry(child, cow_entry));

  // 设置子进程的运行状态
  try(syscall_set_env_status(child, ENV_RUNNABLE));

  return child;
}
