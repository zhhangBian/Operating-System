#include <env.h>
#include <io.h>
#include <mmu.h>
#include <pmap.h>
#include <printk.h>
#include <sched.h>
#include <syscall.h>

// 指向当前进程，在内核态
extern struct Env *curenv;

/* Overview:
 * 	This function is used to print a character on screen.
 *
 * Pre-Condition:
 * 	`c` is the character you want to print.
 */
// 打印一个字符，是所有打印的基础
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
// 打印一个字符串到终端，其实和sys_putchar一致，进行了一定的封装，带有字符串地址检查
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
// 实现用户进程对CPU 的放弃，从而调度其他的进程，本质是强制切换进程，进行一次调度
// 这个函数不会返回
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
// 终止参数envid的进程，由当前进程进行终止
int sys_env_destroy(u_int envid) {
  struct Env *env;
  try(envid2env(envid, &env, 1));

  printk("[%08x] destroying %08x\n", curenv->env_id, env->env_id);
  env_destroy(env);
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
// 注册进程的 页写入异常 处理函数
// 涉及内核态，只能在此由系统调用实现
int sys_set_tlb_mod_entry(u_int envid, u_int func_address) {
  struct Env *env;
  // 获取进程控制块
  try(envid2env(envid, &env, 1));
  // 设置异常处理函数
  env->env_user_tlb_mod_entry = func_address;

  return 0;
}

/* Overview:
 *   Check 'va' is illegal or not, according to include/mmu.h
 */
// 判断是否为正常用户空间虚拟地址，错误的话会返回真
// 内联函数，不会修改栈帧
static inline int is_illegal_va(u_long va) {
  return (va < UTEMP) || (va >= UTOP);
}

static inline int is_illegal_va_range(u_long va, u_int len) {
  if (len == 0) {
    return 0;
  }
  return (va + len < va) || (va < UTEMP) || (va + len > UTOP);
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
 */
// 分配内存：给进程虚拟地址寻找一个物理内存块，建立映射关系
int sys_mem_alloc(u_int envid, u_int virtual_address, u_int permission) {
  struct Env *env;
  struct Page *page_pointer;

  // 检查地址合法性
  if (is_illegal_va(virtual_address)) {
    return -E_INVAL;
  }

  // 获取envid对应的进程控制块，始终查询是否为当前进程的子进程
  // 如果不为子进程：对没有亲缘关系的进程操作是不安全不合理的
  try(envid2env(envid, &env, 1));

  // 获取一个空闲的物理页面控制块
  try(page_alloc(&page_pointer));

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
 */
// 共共享物理页面，将目标进程的虚拟地址映射到  源进程虚拟地址对应的物理内存
int sys_mem_map(u_int src_envid, u_int src_virtual_address,
                u_int dst_envid, u_int dst_virtual_address,
                u_int permission) {
  // 获取需要操作的进程控制块
  struct Env *src_env;
  struct Env *dst_env;
  // 所需要建立映射关系的物理页面
  struct Page *page;

  // 判断虚拟地址是否位于用户空间
  if (is_illegal_va(src_virtual_address) || is_illegal_va(dst_virtual_address)) {
    return -E_INVAL;
  }

  // 获取源进程块和目标进程块
  try(envid2env(src_envid, &src_env, 1));
  try(envid2env(dst_envid, &dst_env, 1));

  // 获取需要被空闲的物理页面
  page = page_lookup(src_env->env_pgdir, src_virtual_address, NULL);
  if (page == NULL) {
    return -E_INVAL;
  }

  // 将 目标进程对应的虚拟地址  与  源对应的物理页面  建立映射关系
  return page_insert(dst_env->env_pgdir, dst_env->env_asid, page, dst_virtual_address, permission);
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
 */
// 实现fork函数，创建当前进程的一个子进程
int sys_exofork(void) {
  // 子进程的进程控制块
  struct Env *env;
  // 初始化一个新的进程控制块
  try(env_alloc(&env, curenv->env_id));

  // 复制父进程调用系统操作时的现场，并不一定等同于curenv->tf中的信息
  // curenv->env_tf的栈帧还没有更新，只有一处进行了env_tf的赋值，是在env_run函数中
  env->env_tf = *((struct Trapframe *)KSTACKTOP - 1);

  // 将子进程的返回值（envid）设置为0
  env->env_tf.regs[2] = 0;
  // 还在进行初始化，设置为不可调度
  env->env_status = ENV_NOT_RUNNABLE;
  // 继承父进程的优先级
  env->env_pri = curenv->env_pri;

  // sigcation
  env->sig_now = curenv->sig_now;
  env->sig_to_handle = curenv->sig_to_handle;
  env->sig_entry = curenv->sig_entry;
  for(int i = 0; i < 64; i++) {
    env->act[i] = curenv->act[i];
  }
  env->sig_mask_pos = curenv->sig_mask_pos;
  for(int i = 0; i < 32; i++) {
    env->sig_mask_stack[i] = curenv->sig_mask_stack[i];
  }

  return env->env_id;
}

/* Overview:
 *   Set 'envid''s 'env_status' to 'status' and update 'env_sched_list'.
 *
 * Post-Condition:
 *   Returns 0 on success.
 *   Returns -E_INVAL if 'status' is neither 'ENV_RUNNABLE' nor 'ENV_NOT_RUNNABLE'.
 *   Returns the original error if underlying calls fail.
 */
// 根据设定的状态将进程加入或移除调度队列
int sys_set_env_status(u_int envid, u_int status) {
  struct Env *env;

  // 检查想要设置的进程状态是否有效
  if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE) {
    return -E_INVAL;
  }

  // 获取进程控制块
  try(envid2env(envid, &env, 1));

  // 将进程从对应的队列中进行操作
  // 如果设置为不运行且当前在运行，则移出调度队列
  if (status == ENV_NOT_RUNNABLE && env->env_status != ENV_NOT_RUNNABLE) {
    TAILQ_REMOVE(&env_sched_list, env, env_sched_link);
  }
  // 如果设置为运行且当前不在运行，则加入调度队列
  else if (status == ENV_RUNNABLE && env->env_status != ENV_RUNNABLE) {
    TAILQ_INSERT_TAIL(&env_sched_list, env, env_sched_link);
  }
  // 设置进程的状态
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
// 将进程的trap frame修改为传入的参数struct Trapframe *tf对应的 trap frame
// 当从该系统调用返回时，将返回设置的栈帧中epc的位置
int sys_set_trapframe(u_int envid, struct Trapframe *tf) {
  // 检查地址是否合法
  if (is_illegal_va_range((u_long)tf, sizeof *tf)) {
    return -E_INVAL;
  }

  // 获取参数envid对应的进程控制块
  struct Env *env;
  try(envid2env(envid, &env, 1));

  // 如果就是当前进程，直接设置
  if (env == curenv) {
    *((struct Trapframe *)KSTACKTOP - 1) = *tf;
    // return `tf->regs[2]` instead of 0, because return value overrides regs[2] on
    // current trapframe.
    // 返回当前异常栈的返回值，否则会改变当前进程的返回值
    return tf->regs[2];
  }
  // 如果不是当前进程，则直接设置
  else {
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
// 进程崩溃，释放信息
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
// 设置相应的握手信号，并将自身进程阻塞，等待发送进程发送完成
int sys_ipc_recv(u_int dst_virtual_address) {
  // 检查地址是否合法
  if (dst_virtual_address != 0 && is_illegal_va(dst_virtual_address)) {
    return -E_INVAL;
  }

  // 进行通信前的准备工作：握手，表明该进程准备接受发送方的消息
  // 进行接受是手动调用的，设置自身为接受态
  curenv->env_ipc_recving = 1;
  // 表明自己要将接受到的页面与dst_va成映射
  curenv->env_ipc_dstva= dst_virtual_address;
  // 阻塞当前进程，等待对方进程发送数据
  // 阻塞的实现：直接移出调度队列，等待持有锁的资源手动调度阻塞进程
  curenv->env_status = ENV_NOT_RUNNABLE;
  TAILQ_REMOVE(&env_sched_list, curenv, env_sched_link);

  // 设置返回值：通过修改当前异常栈
  // 内核态->用户态的返回直接返回值受到一层状态转换的限制，需要通过栈帧实现
  ((struct Trapframe *)KSTACKTOP - 1)->regs[2] = 0;

  // 强制切换进程，让对方进程发送数据，直到接受到信息
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
int sys_ipc_try_send(
  u_int envid_receive,  // 接受信息的进程的envid
  u_int value_send,     // 发送的值，可以只发送值
  u_int src_virtual_address, // 发送进程想要共享的内存的起始虚拟地址
  u_int permission      // 接受方得到的 共享的内存的权限
  ) {
  struct Env *env_receive;
  struct Page *page_shared;

  // 检查地址是否合法
  if (src_virtual_address != 0 && is_illegal_va(src_virtual_address)) {
    return -E_INVAL;
  }

  // 获取**接收进程**的进程控制块
  // 这里是唯一不查找父子关系的地方：发送是任意的，不要求父子关系
  try(envid2env(envid_receive, &env_receive, 0));

  // 握手信号：检查接收进程是否处于接收态
  if (!env_receive->env_ipc_recving) {
    return -E_IPC_NOT_RECV;
  }

  // 设置接收进程的相关属性
  // 直接接受到的值
  env_receive->env_ipc_value = value_send;
  // 接受进程记录发送进程的envid
  env_receive->env_ipc_from = curenv->env_id;
  // 接受方对共享页面的权限操作
  env_receive->env_ipc_perm = PTE_V | permission;
  // 置0表示接受到信息
  env_receive->env_ipc_recving = 0;

  // 如果为0表示只传值，不用共享页面
  // 将当前进程的一个页面共享到接收进程，通过该页面获得发送进程发送的一些信息。
  if (src_virtual_address != 0) {
    // 获取 共享虚拟地址对应的 物理页面控制块
    page_shared = page_lookup(curenv->env_pgdir, src_virtual_address, NULL);
    if (page_shared == NULL) {
      return -E_INVAL;
    }

    // 在接受进程中建立映射关系
    try(page_insert(
          env_receive->env_pgdir, // 接受进程的页目录
          env_receive->env_asid,  // 接受进程的asid
          page_shared,            // 共享的物理页面控制块
          env_receive->env_ipc_dstva, // 接受到的页面 映射到 接收进程的虚拟地址
          permission              // 接受方得到的 共享的内存的权限
        ));
  }

  // 接收到了信息，取消接收进程的阻塞状态
  env_receive->env_status = ENV_RUNNABLE;
  // 如果进程被阻塞了，则不管，直到别的进程将被阻塞进程重新移入调度队列中
  TAILQ_INSERT_TAIL(&env_sched_list, env_receive, env_sched_link);

  return 0;
}

// 读入一个字符，一切输入的起始
int sys_cgetc(void) {
  int ch;
  // 忙等待，阻塞所有进程，直到真正读入字符
  while ((ch = scancharc()) == 0) {}
  return ch;
}

#define CONSOLE_BEGIN (0x180003f8)
#define CONSOLE_END   (0x180003f8 + 0x20)
#define IDE_BEGIN     (0x180001f0)
#define IDE_END       (0x180001f0 + 0x8)

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
 *  All valid device and their physical address ranges:
 *	* ---------------------------------*
 *	|   device   | start addr | length |
 *	* -----------+------------+--------*
 *	|  console   | 0x180003f8 | 0x20   |
 *	|  IDE disk  | 0x180001f0 | 0x8    |
 *	* ---------------------------------*
 */
// 向设备写入
// 从虚拟地址data_addr读取长度为len的数据，写入到对应的device_addr中
// device_addr位于kseg1区，不需要经过cache，由硬件直接完成地址转换
int sys_write_dev(u_int data_addr, u_int device_addr, u_int data_len) {
  // 判断数据所在的虚拟地址是否合法
  if (is_illegal_va_range(data_addr, data_len)) {
    return -E_INVAL;
  }
  // 检查设备地址合法性
  if (!((CONSOLE_BEGIN <= device_addr && device_addr + data_len <= CONSOLE_END) ||
        (IDE_BEGIN     <= device_addr && device_addr + data_len <= IDE_END))) {
    return -E_INVAL;
  }
  // 判断数据长度是否满足要求
  switch (data_len) {
    case 1:
      iowrite8(*(uint8_t *)data_addr, device_addr);
      break;
    case 2:
      iowrite16(*(uint16_t *)data_addr, device_addr);
      break;
    case 4:
      iowrite32(*(uint32_t *)data_addr, device_addr);
      break;
    default:
      return -E_INVAL;
  }

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
 */
// 从设备读入
int sys_read_dev(u_int data_addr, u_int device_addr, u_int data_len) {
  // 判断数据所在的虚拟地址是否合法
  if (is_illegal_va_range(data_addr, data_len)) {
    return -E_INVAL;
  }
  // 检查设备地址合法性
  if (!((CONSOLE_BEGIN <= device_addr && device_addr + data_len <= CONSOLE_END) ||
        (IDE_BEGIN     <= device_addr && device_addr + data_len <= IDE_END))) {
    return -E_INVAL;
  }
  // 判断数据长度是否满足要求
  switch (data_len) {
    case 1:
      *(uint8_t *) data_addr = ioread8(device_addr);
      break;
    case 2:
      *(uint16_t *)data_addr = ioread16(device_addr);
      break;
    case 4:
      *(uint32_t *)data_addr = ioread32(device_addr);
      break;
    default:
      return -E_INVAL;
      break;
  }

  return 0;
}

// 信号发送函数
// 当envid为0时，代表向自身发送信号
// 当envid对应进程不存在，或者sig不符合定义范围时，返回异常码-1。
int sys_kill(u_int envid, int sig) {
  // 检查进程号是否合理
  if (sig > 32 || sig < 1) {
    return -1;
  }

  // 获取对应的进程块
  struct Env *env;
  if (envid2env(envid, &env, 0) < 0) {
    return -1;
  }

  env->sig_to_handle |= GET_SIG(sig);
  // printk("kill sig %d in env %d %d %d\n",sig,env->env_id,env->sig_to_handle,env->sig_shield);

  return 0;
}

// 信号注册函数：设置某信号的处理函数
// 要操作的信号  要设置的对信号的新处理方式  原来对信号的处理方式
// 成功返回0，失败返回-1
int sys_sigaction(int sig_num, struct sigaction *new_act, struct sigaction *old_act) {
  if(sig_num > 32 || sig_num < 1) {
    return -1;
  }

  if(old_act != NULL) {
    *old_act = curenv->act[sig_num];
  }
  if(new_act != NULL && sig_num != SIGKILL) {
    curenv->act[sig_num] = *new_act;
    // printk("debug: %x register %d\n",curenv->env_id, sig_num);
  }

  return 0;
}

// 更改当前进程的信号屏蔽字
int sys_set_sig_shield(int how, sigset_t *new_set, sigset_t *old_set) {
  uint32_t sig_shield = curenv->sig_mask_stack[curenv->sig_mask_pos];
  // 保存原有的信号屏蔽字
  if(old_set != NULL) {
    old_set->sig = sig_shield;
  }

  // 按照方式设置新信号屏蔽字
  if(new_set != NULL) {
    switch (how) {
      case SIG_BLOCK:
        sig_shield |= new_set->sig;
        break;
      case SIG_UNBLOCK:
        sig_shield &= ~new_set->sig;
        break;
      case SIG_SETMASK:
        sig_shield = new_set->sig;
        break;
      default:
        return -1;
        break;
    }
  }

  curenv->sig_mask_stack[curenv->sig_mask_pos] = sig_shield;

  return 0;
}

// 获取当前进程等待处理的信号
int sys_get_sig_pending(sigset_t *set) {
  set->sig = curenv->sig_to_handle;
}

// 设置当前进程的信号处理函数地址（在用户态）
int sys_set_sig_entry(u_int envid, u_int func_address) {
  struct Env *env;
  // 获取进程控制块
  try(envid2env(envid, &env, 0));
  // 设置异常处理函数
  env->sig_entry = func_address;

  return 0;
}

int sys_sig_finish(u_int sig_no) {
  if(sig_no < 1 || sig_no > 32) {
    return -1;
  }

  struct Env *env;
  // 获取进程控制块
  try(envid2env(0, &env, 0));
  // 设置异常处理函数
  env->sig_now = 0;
  env->sig_to_handle &= (~GET_SIG(sig_no));
  env->sig_mask_pos--;

  return 0;
}

// 系统调用函数列表
// 通过函数指针获取其中的函数
void *syscall_table[MAX_SYSNO] = {
    // 打印一个字符，是所有打印的基础
    [SYS_putchar]           = sys_putchar,

    // 打印一个字符串，其实和sys_putchar一致，进行了一定的封装，带有字符串地址检查
    [SYS_print_cons]        = sys_print_cons,

    // 返回当前进程的envid
    [SYS_getenvid]          = sys_getenvid,

    // 实现用户进程对CPU的放弃，强制切换进程，从而调度其他的进程
    [SYS_yield]             = sys_yield,

    // 终止参数envid的进程，由当前进程进行终止
    [SYS_env_destroy]       = sys_env_destroy,

    // 注册进程自身的页写入异常处理函数
    [SYS_set_tlb_mod_entry] = sys_set_tlb_mod_entry,

    // 分配内存：给该程序所允许的虚拟内存空间显式地分配实际的物理内存，建立地址映射关系
    [SYS_mem_alloc]         = sys_mem_alloc,

    // 共享物理页面，将目标进程的虚拟地址映射到  源进程虚拟地址对应的物理内存
    [SYS_mem_map]           = sys_mem_map,

    // 解除映射关系
    [SYS_mem_unmap]         = sys_mem_unmap,

    // 创建子进程：实现fork()
    [SYS_exofork]           = sys_exofork,

    // 根据设定的状态将进程加入或移除调度队列
    [SYS_set_env_status]    = sys_set_env_status,

    // 将参数envid对应的进程的栈帧设置为当前的异常栈
    [SYS_set_trapframe]     = sys_set_trapframe,

    // 进程崩溃，释放信息
    [SYS_panic]             = sys_panic,

    // 进程间通信尝试发送信息
    [SYS_ipc_try_send]      = sys_ipc_try_send,

    // 进程间通信接受信息
    [SYS_ipc_recv]          = sys_ipc_recv,

    // 读入一个字符，一切输入的起始
    [SYS_cgetc]             = sys_cgetc,

    // 向设备写入
    [SYS_write_dev]         = sys_write_dev,

    // 从设备读入
    [SYS_read_dev]          = sys_read_dev,

    // 发送一个信号
    [SYS_SIG_KILL]          = sys_kill,

    // 注册一个信号
    [SYS_SIGACTION]         = sys_sigaction,

    // 更改当前进程的信号屏蔽字
    [SYS_SIG_SHIELD]        = sys_set_sig_shield,

    // 获取当前进程等待处理的信号
    [SYS_SIG_PENDING]       = sys_get_sig_pending,

    // 设置当前进程的信号处理函数地址
    [SYS_SIG_ENTRY]         = sys_set_sig_entry,

    // 完成相应信号的信号处理，设置屏蔽字
    [SYS_SIG_FINISH]        = sys_sig_finish,
};

/* Overview:
 *   Call the function in 'syscall_table' indexed at 'syscall_type' with arguments from user context and
 * stack.
 */
// 调用函数时将栈指针作为参数传递
// 在中断后在entry.S中通过SAVE_ALL保护线程，借助这个栈帧获得用户态信息
// mysyscall函数填入的参数保存在栈指针中
void do_syscall(struct Trapframe *tf) {
  // 定义了一个函数指针func
  int (*syscall_func)(u_int, u_int, u_int, u_int, u_int);

  // 系统调用类型，用户态下的 $a0 寄存器为 regs[4]
  int syscall_type = tf->regs[4];
  // 如果系统调用号不合理
  if (syscall_type < 0 || syscall_type >= MAX_SYSNO) {
    tf->regs[2] = -E_NO_SYS;
    sys_kill(curenv->env_id, SIGSYS);
    tf->cp0_epc += 4;
    return;
  }

  // 返回后执行（syscall的）下一条指令
  tf->cp0_epc += 4;

  // 通过系统调用类型，获取相应的系统调用函数（内核态）
  syscall_func = syscall_table[syscall_type];

  // 取出填入的参数
  // 先取出保存在寄存器中的后三个参数（MIPS只允许最多四个参数）
  u_int arg1 = tf->regs[5];
  u_int arg2 = tf->regs[6];
  u_int arg3 = tf->regs[7];
  // 再取出保存在栈中的其余两个参数
  u_long sp_address = tf->regs[29];
  u_int arg4 = *(u_int *)(sp_address+16);
  u_int arg5 = *(u_int *)(sp_address+20);

  // 将函数指针存入返回值 $v0，存入返回值后由jr执行，在entry.S中
  // 即使不需要这么多参数，也先填入
  // 自动进行压栈操作
  tf->regs[2] = syscall_func(arg1, arg2, arg3, arg4, arg5);
}

// 从内核态返回后，先处理异常
void do_sigaction(struct Trapframe *tf) {
  // 以上在内核态，不用管
  if(tf->cp0_epc >= ULIM || curenv->sig_to_handle == 0) {
    return;
  }
  // printk("hello from do_sigaction  %d\n",curenv->env_id);
  // 当前进程还未处理的信号
  uint32_t sig_to_handle = curenv->sig_to_handle;
  // 当前信号的屏蔽集
  uint32_t sig_shield = curenv->sig_mask_stack[curenv->sig_mask_pos];
  // 找到的信号
  u_int sig_now = 0;

  // 查找当前需要处理的信号
  for(int i = 1; i <= 32; i++) {
    // 如果需要处理 且 未被屏蔽
    if((sig_to_handle & GET_SIG(i)) != 0 && (sig_shield & GET_SIG(i)) == 0) {
      sig_now = i;
      break;
    }
  }

  // 如果有SIGKILL，则直接退出
  if((sig_to_handle & GET_SIG(SIGKILL)) != 0) {
    sig_now = SIGKILL;
  }

  // 没有需要处理的信号，直接返回
  if(sig_now == 0) {
    return;
  }

  // 设置相应的信号处理
  curenv->sig_now = sig_now;
  // 屏蔽当前处理的信号
  sig_shield |= GET_SIG(sig_now);
  sig_shield |= curenv->act[sig_now].sa_mask.sig;
  curenv->sig_mask_stack[++(curenv->sig_mask_pos)] = sig_shield;

  struct Trapframe old_tf = *tf;
  if (tf->regs[29] < USTACKTOP || tf->regs[29] >= UXSTACKTOP) {
    tf->regs[29] = UXSTACKTOP; // 将栈指针指向用户异常处理栈
  }
  // 将当前的 Trapframe压入异常处理栈
  tf->regs[29] -= sizeof(struct Trapframe);
  *(struct Trapframe *)tf->regs[29] = old_tf;
  // 进行信号处理
  if (curenv->sig_entry) {
    // 填入sig_entry的三个参数
    tf->regs[4] = tf->regs[29];
    tf->regs[5] = curenv->act[sig_now].sa_handler;
    tf->regs[6] = sig_now;
    tf->regs[29] -= sizeof(tf->regs[4]) + sizeof(tf->regs[5]) + sizeof(tf->regs[6]);
    // 准备跳转到对应的信号处理函数
    tf->cp0_epc = curenv->sig_entry;
  } else {
    panic("sig but no user handler registered\n");
  }
}