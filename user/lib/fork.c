#include <env.h>
#include <lib.h>
#include <mmu.h>

// envs是暴露给用户空间的进程块数组
// vpd是暴露给用户空间的页目录起始地址，设置了只读权限
// vpt是暴露给用户态的页表的起始地址，设置了只读权限

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
static void /*不返回函数*/__attribute__((noreturn)) cow_entry(struct Trapframe *tf) {
  // 获取出现异常（即写写时复制的页面）时的虚拟地址
  u_int virtual_address = tf->cp0_badvaddr;
  u_int permission;

  // 获取页面的权限
  permission = vpt[VPN(virtual_address)] & 0xfff;
  // 判断权限是否为cow
  if (!(permission & PTE_COW)) {
    user_panic("perm doesn't have PTE_COW");
  }

  // 将原先共享内容复制到了别的地方，不需要再为cow，设置为PTE_D可写权限
  permission = (permission & ~PTE_COW) | PTE_D;

  u_int current_envid = 0;
  // 获得数据拷贝的页控制块，将UCOW作为临时中转的虚拟地址供分配内存
  // 如果一开始就映射到va，会使原本的映射丢失，就不能访问原本va映射到的物理页的内容
  syscall_mem_alloc(current_envid, (void *)UCOW, permission);
  memcpy((void *)UCOW,  // 拷贝数据的dst_va，用UCOW作为周转
         (void *)ROUNDDOWN(virtual_address, PAGE_SIZE), // va不一定对齐，要手动对齐
         PAGE_SIZE);   // 拷贝一页的数据：发生异常的单位是当前页
  // 将发生页写入异常的地址va映射到物理页面上，并设置相关权限
  syscall_mem_map(current_envid, (void *)UCOW, current_envid, (void *)virtual_address, permission);
  // 解除当前进程临时地址UCOW的内存映射，释放UCOW的虚拟地址
  syscall_mem_unmap(current_envid, (void *)UCOW);

  // 在异常处理完成后恢复现场，现在位于用户态，使用系统调用恢复现场
  // 当从该系统调用返回时，将返回设置的栈帧中epc的位置
  // 对于cow_entry来说，意味着恢复到产生TLB Mod时的现场。
  int func_info = syscall_set_trapframe(current_envid, tf);
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
 */
// 设置子进程的地址空间
// 将地址空间中需要与子进程共享的页面映射给子进程，需要遍历父进程的大部分用户空间页
// 修改原先可写、且不是PTE_LIBRARY的页面权限为写时复制
// 其余页面保持原先的权限即可

// - 只读页面：对于不具有PTE_D权限位的页面，按照相同权限（只读）映射给子进程即可。
// - 写时复制页面：即具有PTE_COW权限位的页面。
//   这类页面是**之前的fork时duppage的结果**，且在本次fork前必然未被写入过。
// - 共享页面：即具有PTE_LIBRARY权限位的页面。
//   这类页面需要保持**共享可写**的状态，即在父子进程中映射到相同的物理页，使对其进行**修改的结果相互可见**。
// - 可写页面：即具有PTE_D权限位，且不符合以上特殊情况的页面。
//   这类页面需要在父进程和子进程的页表项中都使用PTE_COW 权限位进行保护。
static void duppage(u_int child_envid, u_int page_number) {
  u_int page_address;
  u_int permission;

  // 获取当前映射页面的虚拟地址（在父进程中，当然子进程也是这个地址）
  page_address = page_number << PGSHIFT;
  // 通过暴露给用户态的页表获取对应页面的权限
  permission = vpt[page_number] & 0xfff;

  u_int set_to_cow = 0;
  // 如果页面可写，且不是PTE_LIBRARY，则在父子进程中同时设置为写时复制
  if ((permission & PTE_D) &&  !(permission & PTE_LIBRARY)) {
    // 写时复制要求不可写
    permission = (permission & ~ PTE_D) | PTE_COW;
    set_to_cow = 1;
  }
  // 其余情况保持页面权限不变

  u_int parent_envid = 0;
  // 设置子进程的虚拟地址空间：共享父进程的页面
  syscall_mem_map(parent_envid, page_address, child_envid, page_address, permission);
  // 修改为cow后，对于父进程设置权限，也利用遍历函数：维护操作统一，不添加新的函数
  if (set_to_cow) {
    syscall_mem_map(parent_envid, page_address, parent_envid, page_address, permission);
  }
}

/* Overview:
 *   User-level 'fork'. Create a child and then copy our address space.
 *   Set up ours and its TLB Mod user exception entry to 'cow_entry'.
 *
 * Post-Conditon:
 *   Child's 'env' is properly set.
 */
// fork()是一个用户态函数！
// 在此，进程进行分叉，原先的行为逻辑一分为二，开始进行独立的运行
int fork(void) {
  u_int fork_return_envid;
  u_int i;

  // 先为父进程设置写时复制异常处理函数
  // 之后涉及函数调用，会修改用户栈，而用户栈已经被共享给了子进程，会涉及写时复制异常问题
  if (env->env_user_tlb_mod_entry != (u_int)cow_entry) {
    // 为什么不直接写：能读取的进程块是由内核暴露的，不能进行写操作，由硬件屏蔽
    try(syscall_set_tlb_mod_entry(/*为自己设置*/0, cow_entry));
  }

  // 调用结束后，创建了一个子进程，处于被阻塞状态，需要被设置
  fork_return_envid = syscall_exofork();
  // 子进程设置完成后会从这里开始

  // 如果是子进程，涉及函数调用，会修改用户栈
  // 初始创建子进程后，子进程处于阻塞状态，等待父进程进行调度
  if (fork_return_envid == 0) {
    // 将env指针指向自身进程控制块：根据id得到，之前还指向父进程
    env = envs + ENVX(syscall_getenvid());
    return 0;
  }

  // 由父进程设置子进程的地址空间，同时将父进程页面权限设置为写时复制PTE_COW
  // duppage对每一个页表项进行操作
  // - 需要遍历所有USTACKTOP之下的地址空间
  // - ULIM到USTACKTOP之间的部分，储存的是页表信息、异常处理栈等，不需要共享
  // - 再往上为内核空间，总被共享
  for (i = 0; i < VPN(USTACKTOP); i++) {
    // 在调用 duppage 之前，判断页目录项和页表项是否有效。
    // 如果不判断则会在 duppage 函数中发生异常（最终是由page_lookup产生的）
    // vpd是页目录项数组，i相当于地址的高20位，需要取得地址的高10位作为页目录的索引
    if ((vpd[i >> 10] & PTE_V) && (vpt[i] & PTE_V) ) {
      duppage(fork_return_envid, i);
    }
  }

  // 为子进程设置写时复制异常处理函数，供do_tlb_mod使用
  // 见tlbex.c
  try(syscall_set_tlb_mod_entry(fork_return_envid, cow_entry));

  // 对子进程设置完毕，将其设置为可以运行，开始子进程波澜壮阔的一生
  try(syscall_set_env_status(fork_return_envid, ENV_RUNNABLE));

  return fork_return_envid;
}
