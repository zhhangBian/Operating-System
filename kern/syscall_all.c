#include <env.h>
#include <io.h>
#include <mmu.h>
#include <pmap.h>
#include <printk.h>
#include <sched.h>
#include <syscall.h>

extern struct Env *curenv;

/* Overview:
 * 	This function is used to print a character on screen.
 *
 * Pre-Condition:
 * 	`c` is the character you want to print.
 */
void sys_putchar(int c) {
  printcharc((char)c);
  return;
}

/* Overview:
 * 	This function is used to print a string of bytes on screen.
 *
 * Pre-Condition:
 * 	`s` is base address of the string, and `num` is length of the string.
 */
int sys_print_cons(const void *s, u_int num) {
  if (((u_int)s + num) > UTOP || ((u_int)s) >= UTOP || (s > s + num)) {
    return -E_INVAL;
  }
  u_int i;
  for (i = 0; i < num; i++) {
    printcharc(((char *)s)[i]);
  }
  return 0;
}

/* Overview:
 *	This function provides the environment id of current process.
 *
 * Post-Condition:
 * 	return the current environment id
 */
u_int sys_getenvid(void) {
  return curenv->env_id;
}

/* Overview:
 *   Give up remaining CPU time slice for 'curenv'.
 *
 * Post-Condition:
 *   Another env is scheduled.
 *
 * Hint:
 *   This function will never return.
 */
// void sys_yield(void);
// 实现用户进程对CPU 的放弃，从而调度其他的进程
void __attribute__((noreturn)) sys_yield(void) {
  // 强制切换进程
  schedule(1);
}

/* Overview:
 * 	This function is used to destroy the current environment.
 *
 * Pre-Condition:
 * 	The parameter `envid` must be the environment id of a
 * process, which is either a child of the caller of this function
 * or the caller itself.
 *
 * Post-Condition:
 *  Returns 0 on success.
 *  Returns the original error if underlying calls fail.
 */
int sys_env_destroy(u_int envid) {
  struct Env *e;
  try(envid2env(envid, &e, 1));

  printk("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
  env_destroy(e);
  return 0;
}

/* Overview:
 *   Register the entry of user space TLB Mod handler of 'envid'.
 *
 * Post-Condition:
 *   The 'envid''s TLB Mod exception handler entry will be set to 'func'.
 *   Returns 0 on success.
 *   Returns the original error if underlying calls fail.
 */
int sys_set_tlb_mod_entry(u_int envid, u_int func) {
  struct Env *env;

  /* Step 1: Convert the envid to its corresponding 'struct Env *' using 'envid2env'. */
  /* Exercise 4.12: Your code here. (1/2) */

  /* Step 2: Set its 'env_user_tlb_mod_entry' to 'func'. */
  /* Exercise 4.12: Your code here. (2/2) */

  return 0;
}

/* Overview:
 *   Check 'va' is illegal or not, according to include/mmu.h
 */
// 判断是否为正常用户空间虚拟地址
static inline int is_illegal_va(u_long va) {
  return va < UTEMP || va >= UTOP;
}

static inline int is_illegal_va_range(u_long va, u_int len) {
  if (len == 0) {
    return 0;
  }
  return va + len < va || va < UTEMP || va + len > UTOP;
}

/* Overview:
 *   Allocate a physical page and map 'va' to it with 'permission' in the address space of 'envid'.
 *   If 'va' is already mapped, that original page is sliently unmapped.
 *   'envid2env' should be used with 'checkperm' set, like in most syscalls, to ensure the target is
 * either the caller or its child.
 *
 * Post-Condition:
 *   Return 0 on success.
 *   Return -E_BAD_ENV: 'checkperm' of 'envid2env' fails for 'envid'.
 *   Return -E_INVAL:   'va' is illegal (should be checked using 'is_illegal_va').
 *   Return the original error: underlying calls fail (you can use 'try' macro).
 *
 * Hint:
 *   You may want to use the following functions:
 *   'envid2env', 'page_alloc', 'page_insert', 'try' (macro)
 */
// 分配内存：给该程序所允许的虚拟内存空间显式地分配实际的物理内存
// 对于OS，是建立一段映射关系，将一个进程运行空间中的某段地址与实际物理内存进行映射
// 从而可以通过该虚拟页面来对物理内存进行存取访问
// 通过传入的进程标识符参数envid来确定发出请求的进程
int sys_mem_alloc(u_int envid, u_int virtual_address, u_int permission) {
  struct Env *env;
  struct Page *page_pointer;

  /* Step 1: Check if 'va' is a legal user virtual address using 'is_illegal_va'. */
  if (is_illegal_va(virtual_address)) {
    return -E_INVAL;
  }

  /* Step 2: Convert the envid to its corresponding 'struct Env *' using 'envid2env'. */
  /* Hint: **Always** validate the permission in syscalls! */
  // 获取envid对应的进程控制块
  // 始终查询是否有亲缘关系
  try(envid2env(envid, &env, 1));

  /* Step 3: Allocate a physical page using 'page_alloc'. */
  // 获取物理页面控制块
  try(page_alloc(&page_pointer));

  /* Step 4: Map the allocated page at 'va' with permissionission 'permission' using 'page_insert'. */
  // 建立地址映射关系
  return page_insert(env->env_pgdir, env->env_asid, page_pointer, virtual_address, permission);
}

/* Overview:
 *   Find the physical page mapped at 'src_virtual_address' in the address space of env 'src_envid', and map 'dst_envid''s
 *   'dst_virtual_address' to it with 'permission'.
 *
 * Post-Condition:
 *   Return 0 on success.
 *   Return -E_BAD_ENV: 'checkpermission' of 'envid2env' fails for 'src_envid' or 'dst_envid'.
 *   Return -E_INVAL: 'src_virtual_address' or 'dst_virtual_address' is illegal, or 'src_virtual_address' is unmapped in 'src_envid'.
 *   Return the original error: underlying calls fail.
 *
 * Hint:
 *   You may want to use the following functions:
 *   'envid2env', 'page_lookup', 'page_insert'
 */
// 共享物理页面，将目标进程的虚拟地址  与  源进程块虚拟地址对应的物理页面  建立映射关系
int sys_mem_map(u_int src_envid, u_int src_virtual_address, 
                u_int dst_envid, u_int dst_virtual_address, 
                u_int permission) {
  struct Env *src_env;
  struct Env *dst_env;
  struct Page *page_pointer;

  // 判断虚拟地址是否位于用户空间
  if (is_illegal_va(src_virtual_address) || is_illegal_va(dst_virtual_address)) {
		return -E_INVAL;
	}

  // 获取源进程块和目标进程块
  try(envid2env(src_envid, &src_env, 1));
  try(envid2env(dst_envid, &dst_env, 1));

  // 获取源进程虚拟地址对应的物理内存控制块
  if ((page_pointer = page_lookup(src_env->env_pgdir, src_virtual_address, NULL)) == NULL) {
		return -E_INVAL;
	}

  // 将目标进程块对应的虚拟地址  与  源对应的物理页面  建立映射关系
  return page_insert(dst_env->env_pgdir, dst_env->env_asid, page_pointer, dst_virtual_address, permission);
}

/* Overview:
 *   Unmap the physical page mapped at 'va' in the address space of 'envid'.
 *   If no physical page is mapped there, this function silently succeeds.
 *
 * Post-Condition:
 *   Return 0 on success.
 *   Return -E_BAD_ENV: 'checkperm' of 'envid2env' fails for 'envid'.
 *   Return -E_INVAL:   'va' is illegal.
 *   Return the original error when underlying calls fail.
 */
// 解除映射关系
int sys_mem_unmap(u_int envid, u_int virtual_address) {
  struct Env *env;
  // 判断虚拟地址是否合法
  if (is_illegal_va(virtual_address)) {
		return -E_INVAL;
	}

  // 获取对应的进程控制块
  try(envid2env(envid, &env, 1));

  // 删除映射关系
  page_remove(env->env_pgdir, env->env_asid, virtual_address);
  return 0;
}

/* Overview:
 *   Allocate a new env as a child of 'curenv'.
 *
 * Post-Condition:
 *   Returns the child's envid on success, and
 *   - The new env's 'env_tf' is copied from the kernel stack, except for $v0 set to 0 to indicate
 *     the return value in child.
 *   - The new env's 'env_status' is set to 'ENV_NOT_RUNNABLE'.
 *   - The new env's 'env_pri' is copied from 'curenv'.
 *   Returns the original error if underlying calls fail.
 *
 * Hint:
 *   This syscall works as an essential step in user-space 'fork' and 'spawn'.
 */
int sys_exofork(void) {
  struct Env *e;

  /* Step 1: Allocate a new env using 'env_alloc'. */
  /* Exercise 4.9: Your code here. (1/4) */

  /* Step 2: Copy the current Trapframe below 'KSTACKTOP' to the new env's 'env_tf'. */
  /* Exercise 4.9: Your code here. (2/4) */

  /* Step 3: Set the new env's 'env_tf.regs[2]' to 0 to indicate the return value in child. */
  /* Exercise 4.9: Your code here. (3/4) */

  /* Step 4: Set up the new env's 'env_status' and 'env_pri'.  */
  /* Exercise 4.9: Your code here. (4/4) */

  return e->env_id;
}

/* Overview:
 *   Set 'envid''s 'env_status' to 'status' and update 'env_sched_list'.
 *
 * Post-Condition:
 *   Returns 0 on success.
 *   Returns -E_INVAL if 'status' is neither 'ENV_RUNNABLE' nor 'ENV_NOT_RUNNABLE'.
 *   Returns the original error if underlying calls fail.
 *
 * Hint:
 *   The invariant that 'env_sched_list' contains and only contains all runnable envs should be
 *   maintained.
 */
int sys_set_env_status(u_int envid, u_int status) {
  struct Env *env;

  /* Step 1: Check if 'status' is valid. */
  /* Exercise 4.14: Your code here. (1/3) */

  /* Step 2: Convert the envid to its corresponding 'struct Env *' using 'envid2env'. */
  /* Exercise 4.14: Your code here. (2/3) */

  /* Step 3: Update 'env_sched_list' if the 'env_status' of 'env' is being changed. */
  /* Exercise 4.14: Your code here. (3/3) */

  /* Step 4: Set the 'env_status' of 'env'. */
  env->env_status = status;
  return 0;
}

/* Overview:
 *  Set envid's trap frame to 'tf'.
 *
 * Post-Condition:
 *  The target env's context is set to 'tf'.
 *  Returns 0 on success (except when the 'envid' is the current env, so no value could be
 * returned).
 *  Returns -E_INVAL if the environment cannot be manipulated or 'tf' is invalid.
 *  Returns the original error if other underlying calls fail.
 */
int sys_set_trapframe(u_int envid, struct Trapframe *tf) {
  if (is_illegal_va_range((u_long)tf, sizeof *tf)) {
    return -E_INVAL;
  }
  struct Env *env;
  try(envid2env(envid, &env, 1));
  if (env == curenv) {
    *((struct Trapframe *)KSTACKTOP - 1) = *tf;
    // return `tf->regs[2]` instead of 0, because return value overrides regs[2] on
    // current trapframe.
    return tf->regs[2];
  } else {
    env->env_tf = *tf;
    return 0;
  }
}

/* Overview:
 * 	Kernel panic with message `msg`.
 *
 * Post-Condition:
 * 	This function will halt the system.
 */
void sys_panic(char *msg) {
  panic("%s", TRUP(msg));
}

/* Overview:
 *   Wait for a message (a value, together with a page if 'dst_virtual_address' is not 0) from other envs.
 *   'curenv' is blocked until a message is sent.
 *
 * Post-Condition:
 *   Return 0 on success.
 *   Return -E_INVAL: 'dst_virtual_address' is neither 0 nor a legal address.
 */
int sys_ipc_recv(u_int dst_virtual_address) {
  /* Step 1: Check if 'dst_virtual_address' is either zero or a legal address. */
  if (dst_virtual_address != 0 && is_illegal_va(dst_virtual_address)) {
    return -E_INVAL;
  }

  /* Step 2: Set 'curenv->env_ipc_recving' to 1. */
  /* Exercise 4.8: Your code here. (1/8) */

  /* Step 3: Set the value of 'curenv->env_ipc_dst_virtual_address'. */
  /* Exercise 4.8: Your code here. (2/8) */

  /* Step 4: Set the status of 'curenv' to 'ENV_NOT_RUNNABLE' and remove it from
   * 'env_sched_list'. */
  /* Exercise 4.8: Your code here. (3/8) */

  /* Step 5: Give up the CPU and block until a message is received. */
  ((struct Trapframe *)KSTACKTOP - 1)->regs[2] = 0;
  schedule(1);
}

/* Overview:
 *   Try to send a 'value' (together with a page if 'src_virtual_address' is not 0) to the target env 'envid'.
 *
 * Post-Condition:
 *   Return 0 on success, and the target env is updated as follows:
 *   - 'env_ipc_recving' is set to 0 to block future sends.
 *   - 'env_ipc_from' is set to the sender's envid.
 *   - 'env_ipc_value' is set to the 'value'.
 *   - 'env_status' is set to 'ENV_RUNNABLE' again to recover from 'ipc_recv'.
 *   - if 'src_virtual_address' is not NULL, map 'env_ipc_dst_virtual_address' to the same page mapped at 'src_virtual_address' in 'curenv'
 *     with 'permission'.
 *
 *   Return -E_IPC_NOT_RECV if the target has not been waiting for an IPC message with
 *   'sys_ipc_recv'.
 *   Return the original error when underlying calls fail.
 */
int sys_ipc_try_send(u_int envid, u_int value, u_int src_virtual_address, u_int permission) {
  struct Env *e;
  struct Page *p;

  /* Step 1: Check if 'src_virtual_address' is either zero or a legal address. */
  /* Exercise 4.8: Your code here. (4/8) */

  /* Step 2: Convert 'envid' to 'struct Env *e'. */
  /* This is the only syscall where the 'envid2env' should be used with 'checkperm' UNSET,
   * because the target env is not restricted to 'curenv''s children. */
  /* Exercise 4.8: Your code here. (5/8) */

  /* Step 3: Check if the target is waiting for a message. */
  /* Exercise 4.8: Your code here. (6/8) */

  /* Step 4: Set the target's ipc fields. */
  e->env_ipc_value = value;
  e->env_ipc_from = curenv->env_id;
  e->env_ipc_perm = PTE_V | permission;
  e->env_ipc_recving = 0;

  /* Step 5: Set the target's status to 'ENV_RUNNABLE' again and insert it to the tail of
   * 'env_sched_list'. */
  /* Exercise 4.8: Your code here. (7/8) */

  /* Step 6: If 'src_virtual_address' is not zero, map the page at 'src_virtual_address' in 'curenv' to 'e->env_ipc_dst_virtual_address'
   * in 'e'. */
  /* Return -E_INVAL if 'src_virtual_address' is not zero and not mapped in 'curenv'. */
  if (src_virtual_address != 0) {
    /* Exercise 4.8: Your code here. (8/8) */

  }
  return 0;
}

// XXX: kernel does busy waiting here, blocking all envs
int sys_cgetc(void) {
  int ch;
  while ((ch = scancharc()) == 0) {
  }
  return ch;
}

/* Overview:
 *  This function is used to write data at 'va' with length 'len' to a device physical address
 *  'pa'. Remember to check the validity of 'va' and 'pa' (see Hint below);
 *
 *  'va' is the starting address of source data, 'len' is the
 *  length of data (in bytes), 'pa' is the physical address of
 *  the device (maybe with a offset).
 *
 * Pre-Condition:
 *  'len' must be 1, 2 or 4, otherwise return -E_INVAL.
 *
 * Post-Condition:
 *  Data within [va, va+len) is copied to the physical address 'pa'.
 *  Return 0 on success.
 *  Return -E_INVAL on bad address.
 *
 * Hint:
 *  You can use 'is_illegal_va_range' to validate 'va'.
 *  You may use the unmapped and uncached segment in kernel address space (KSEG1)
 *  to perform MMIO by assigning a corresponding-lengthed data to the address,
 *  or you can just simply use the io function defined in 'include/io.h',
 *  such as 'iowrite32', 'iowrite16' and 'iowrite8'.
 *
 *  All valid device and their physical address ranges:
 *	* ---------------------------------*
 *	|   device   | start addr | length |
 *	* -----------+------------+--------*
 *	|  console   | 0x180003f8 | 0x20   |
 *	|  IDE disk  | 0x180001f0 | 0x8    |
 *	* ---------------------------------*
 */
int sys_write_dev(u_int va, u_int pa, u_int len) {
  /* Exercise 5.1: Your code here. (1/2) */

  return 0;
}

/* Overview:
 *  This function is used to read data from a device physical address.
 *
 * Pre-Condition:
 *  'len' must be 1, 2 or 4, otherwise return -E_INVAL.
 *
 * Post-Condition:
 *  Data at 'pa' is copied from device to [va, va+len).
 *  Return 0 on success.
 *  Return -E_INVAL on bad address.
 *
 * Hint:
 *  You can use 'is_illegal_va_range' to validate 'va'.
 *  You can use function 'ioread32', 'ioread16' and 'ioread8' to read data from device.
 */
int sys_read_dev(u_int va, u_int pa, u_int len) {
  /* Exercise 5.1: Your code here. (2/2) */

  return 0;
}

// 系统调用函数列表
// 通过函数指针获取其中的函数
void *syscall_table[MAX_SYSNO] = {
    [SYS_putchar] = sys_putchar,
    [SYS_print_cons] = sys_print_cons,
    [SYS_getenvid] = sys_getenvid,
    // 实现用户进程对CPU 的放弃，从而调度其他的进程
    [SYS_yield] = sys_yield,
    [SYS_env_destroy] = sys_env_destroy,
    [SYS_set_tlb_mod_entry] = sys_set_tlb_mod_entry,
    // 分配内存：给该程序所允许的虚拟内存空间显式地分配实际的物理内存
    [SYS_mem_alloc] = sys_mem_alloc,
    // 共享物理页面，将源进程和目标进程  的相应内存映射到相同的相应地址
    [SYS_mem_map] = sys_mem_map,
    // 解除映射关系
    [SYS_mem_unmap] = sys_mem_unmap,
    [SYS_exofork] = sys_exofork,
    [SYS_set_env_status] = sys_set_env_status,
    [SYS_set_trapframe] = sys_set_trapframe,
    [SYS_panic] = sys_panic,
    [SYS_ipc_try_send] = sys_ipc_try_send,
    [SYS_ipc_recv] = sys_ipc_recv,
    [SYS_cgetc] = sys_cgetc,
    [SYS_write_dev] = sys_write_dev,
    [SYS_read_dev] = sys_read_dev,
};

/* Overview:
 *   Call the function in 'syscall_table' indexed at 'syscall_type' with arguments from user context and
 * stack.
 *
 * Hint:
 *   Use syscall_type from $a0 to dispatch the syscall.
 *   The possible arguments are stored at $a1, $a2, $a3, [$sp + 16 bytes], [$sp + 20 bytes] in
 *   order.
 *   Number of arguments cannot exceed 5.
 */
// 调用函数时将栈指针作为参数传递
// mysyscall函数填入的参数保存在栈指针中
void do_syscall(struct Trapframe *tf) {
  // 定义了一个函数指针func
  int (*syscall_func)(u_int, u_int, u_int, u_int, u_int);

  // 用户态下的 $a0 寄存器为 regs[4]
  int syscall_type = tf->regs[4];
  // 系统调用号不合理
  if (syscall_type < 0 || syscall_type >= MAX_SYSNO) {
    tf->regs[2] = -E_NO_SYS;
    return;
  }

  // 返回后执行（syscall的）下一条指令
  tf->cp0_epc += 4;

  // 获取系统调用的类型
  syscall_func = syscall_table[syscall_type];

  // 取出填入的参数

  // 先取出保存在寄存器中的前三个参数
  u_int arg1 = tf->regs[5];
  u_int arg2 = tf->regs[6];
  u_int arg3 = tf->regs[7];
  // 再取出保存在栈中的其余两个参数
  u_long sp_address = tf->regs[29];
  u_int arg4 = *(u_int *)(sp_address+16);
  u_int arg5 = *(u_int *)(sp_address+20);

  // 将函数指针存入返回值 $v0
  tf->regs[2] = func(arg1, arg2, arg3, arg4, arg5);
}
