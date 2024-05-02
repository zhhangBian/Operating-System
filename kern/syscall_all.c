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
// 返回当前进程的envid
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
// 注册进程自身的页写入异常处理函数
int sys_set_tlb_mod_entry(u_int envid, u_int func) {
  struct Env *env;
  // 获取进程控制块
  try(envid2env(envid, &env, 1));
  // 设置异常处理函数
  env->env_user_tlb_mod_entry = func;

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
// 实现fork函数
int sys_exofork(void) {
  struct Env *env;
  // 初始化一个新的进程控制块
  try(env_alloc(&env, curenv->env_id));

  // 复制父进程调用系统操作时的现场
  env->env_tf = *((struct Trapframe *)KSTACKTOP - 1);

  // 将返回值（子进程envid）设置为0：这里是在初始化子进程
  env->env_tf.regs[2] = 0;
  // 设置为不可调度
  env->env_status = ENV_NOT_RUNNABLE;
  env->env_pri = curenv->env_pri;

  return env->env_id;
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
// 根据设定的状态将进程加入或移除调度队列
int sys_set_env_status(u_int envid, u_int status) {
  struct Env *env;

  // 检查进程状态是否有效
  if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE) {
    return -E_INVAL;
  }

  // 获取进程控制块
  try(envid2env(envid, &env, 1));

  // 设置进程的状态
  if (env->env_status != ENV_NOT_RUNNABLE && status == ENV_NOT_RUNNABLE) {
    TAILQ_REMOVE(&env_sched_list, env, env_sched_link);
  }
  else if (env->env_status != ENV_RUNNABLE && status == ENV_RUNNABLE) {
    TAILQ_INSERT_TAIL(&env_sched_list, env, env_sched_link);
  }

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
// 进程间通信接收信息
int sys_ipc_recv(u_int dst_virtual_address) {
  // 检查地址是否合法
  if (dst_virtual_address != 0 && is_illegal_va(dst_virtual_address)) {
    return -E_INVAL;
  }

  // 进行通信前的准备工作：握手
  // 表明该进程准备接受发送方的消息
  curenv->env_ipc_recving = 1;
  // 表明自己要将接受到的页面与dstva成映射
  curenv->env_ipc_dstva= dst_virtual_address;

  // 阻塞当前进程，等待对方进程发送数据
  curenv->env_status = ENV_NOT_RUNNABLE;
  TAILQ_REMOVE(&env_sched_list, curenv, env_sched_link);
  // 强制切换进程，让对方进程发送数据，指导接受到信息
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
// 进程间通信发送信息
int sys_ipc_try_send(u_int envid_receive, u_int value_send, u_int src_virtual_address, u_int permission) {
  struct Env *env_receive;
  struct Page *page_shared;

  // 检查地址是否合法
  if (src_virtual_address != 0 && is_illegal_va(src_virtual_address)) {
    return -E_INVAL;
  }

  /* This is the only syscall where the 'envid2env' should be used with 'checkperm' UNSET,
   * because the target env is not restricted to 'curenv''s children. */
  // 获取**接受进程**的进程控制块
  // 这里是唯一不查找父子关系的地方：发送是任意的
  try(envid2env(envid_receive, &env_receive, 0));

  // 检查接收进程是否处于接收态
  if (!env_receive->env_ipc_recving) {
    return -E_IPC_NOT_RECV;
  }

  // 设置接收进程的相关属性
  env_receive->env_ipc_value = value_send;
  env_receive->env_ipc_from = curenv->env_id;
  env_receive->env_ipc_perm = PTE_V | permission;
  // 置0表示接受到信息
  env_receive->env_ipc_recving = 0;

  // 接收到了信息，取消接收进程的阻塞状态
  env_receive->env_status = ENV_RUNNABLE;
  TAILQ_INSERT_TAIL(&env_sched_list, env_receive, env_sched_link);

  // 将当前进程的一个页面共享到接收进程。只有这样，接收进程才能通过该页面获得发送进程发送的一些信息。
  // 如果为0表示只传值，不用共享页面
  if (src_virtual_address != 0) {
    // 获取当前进程虚拟地址对应的  物理页面控制块
    page_shared = page_lookup(curenv->env_pgdir, src_virtual_address, NULL);
    if (page_shared == NULL) {
      return -E_INVAL;
    }
    // 在接受进程中建立映射关系
    try(page_insert(env_receive->env_pgdir,
                    env_receive->env_asid,
                    page_shared,
                    env_receive->env_ipc_dstva,
                    permission));
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
    [SYS_putchar]           = sys_putchar,
    [SYS_print_cons]        = sys_print_cons,

    // 返回当前进程的envid
    [SYS_getenvid]          = sys_getenvid,

    // 实现用户进程对CPU的放弃，强制切换进程，从而调度其他的进程
    [SYS_yield]             = sys_yield,

    [SYS_env_destroy]       = sys_env_destroy,

    // 注册进程自身的页写入异常处理函数
    [SYS_set_tlb_mod_entry] = sys_set_tlb_mod_entry,

    // 分配内存：给该程序所允许的虚拟内存空间显式地分配实际的物理内存
    [SYS_mem_alloc]         = sys_mem_alloc,

    // 共享物理页面，将源进程和目标进程  的相应内存映射到相同的相应地址
    [SYS_mem_map]           = sys_mem_map,

    // 解除映射关系
    [SYS_mem_unmap]         = sys_mem_unmap,

    // 创建子进程：实现fork()
    [SYS_exofork]           = sys_exofork,

    // 根据设定的状态将进程加入或移除调度队列
    [SYS_set_env_status]    = sys_set_env_status,
    [SYS_set_trapframe]     = sys_set_trapframe,
    [SYS_panic]             = sys_panic,

    // 进程间通信尝试发送信息
    [SYS_ipc_try_send]      = sys_ipc_try_send,

    // 进程间通信接受信息
    [SYS_ipc_recv]          = sys_ipc_recv,

    [SYS_cgetc]             = sys_cgetc,
    [SYS_write_dev]         = sys_write_dev,
    [SYS_read_dev]          = sys_read_dev,
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
  tf->regs[2] = syscall_func(arg1, arg2, arg3, arg4, arg5);
}
