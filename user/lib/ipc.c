// User-level IPC library routines
// 用户态的IPC函数
// 在此可以使用内核态暴露的envs和pages

// IPC的方式：
// 1. 发送方调用ipc_send，开始忙等待，直至发送成功
// 2. 需要接收方手动调用ipc_recv，才可以实现一次完整的通信

#include <env.h>
#include <lib.h>
#include <mmu.h>

// 用户态的ipc_send函数
// 尝试发送信息直至成功，对CPU不友好，唯一区分的就是是否发送成功，不区分具体情况
// 使用srcva为0的调用来表示只传value值，而不需要传递物理页面，
void ipc_send(u_int receive_id, u_int value, const void *src_va, u_int perm) {
  int func_info;
  // 轮询直至接受到信息
  // 信息的接受是由接受进程手动设置的，如果对方进程一直不接受，则会一直阻塞
  while ((func_info = syscall_ipc_try_send(receive_id, value, src_va, perm)) == -E_IPC_NOT_RECV) {
    // 使用yield（用户态）让出CPU，避免无意义轮询
    // 没有区分具体的发送失败原因：阻塞/就是失败等
    syscall_yield();
  }

  user_assert(func_info == 0);
}

// 用户态的ipc_receive函数，实际设置相应的握手信号，被发送进程设置相应信息，直至完成
// 返回 发送的值-发送进程的id-共享页面的权限  第一个作为返回值，另外两个通过指针实现
u_int ipc_recv(u_int *send_id_pointer, // 记录发送进程的id，通过指针完成
               void *dst_va,           // 共享信息存储到的虚拟地址，由自身设置
               u_int *permission_pointer  // 记录共享页面设置的权限，通过指针完成
  ) {
  // 手动开始接受信息，调用系统调用，由此陷入内核态
  // 设置相应的握手信号，并将自身进程阻塞，等待发送进程发送完成
  int func_info = syscall_ipc_recv(dst_va);
  if (func_info != 0) {
    user_panic("syscall_ipc_recv err: %d", func_info);
  }
  // 进程被阻塞，发送进程开始工作
  // 。。。
  // 在这里，发送进程已经完成了信息的发送，通过栈帧回到函数
  // 完成信息发送，进行善后工作

  // 记录发送进程的id，通过指针完成
  if (send_id_pointer) {
    *send_id_pointer = env->env_ipc_from;
  }
  // 记录共享页面设置的权限，通过指针完成
  if (permission_pointer) {
    *permission_pointer = env->env_ipc_perm;
  }
  // 直接返回共享的值
  return env->env_ipc_value;
}

/*
当一个信号被发送给一个进程时，内核会中断进程的正常控制流，转而执行与该信号相关的用户态处理函数进行处理
在执行该处理函数前，会将该信号所设置的信号屏蔽集加入到进程的信号屏蔽集中
在执行完该用户态处理函数后，又会将恢复原来的信号屏蔽集
本次实验只需实现[1,32]普通信号，无需考虑[33,64]的实时信号。
*/

static inline int isValidSig(int sig_no) {
  return sig_no >= 1 && sig_no <= 32;
} 

// 信号注册函数：设置某信号的处理函数
// 要操作的信号  要设置的对信号的新处理方式  原来对信号的处理方式
// 成功返回0，失败返回-1
int sigaction(int signum, const struct sigaction *newact, struct sigaction *oldact) {
  // 判断信号是否合理
  if (!isValidSig(signum)) {
    return -1;
  }

  set_sigaction_entry();

  return syscall_sigaction(signum, newact, oldact);
}

// 信号发送函数
// 当envid为0时，代表向自身发送信号
// 当envid对应进程不存在，或者sig不符合定义范围时，返回异常码-1。
int kill(u_int envid, int sig) {
  if (!isValidSig(sig)) {
    return -1;
  }

  return syscall_kill(envid, sig);
}

// 清空参数中的__set掩码，初始化信号集以排除所有信号。这意味着__set将不包含任何信号。(清0)
int sigemptyset(sigset_t *__set) {
  __set->sig = 0;
  return 0;
}

// 将参数中的__set掩码填满，使其包含所有已定义的信号。这意味着__set将包括所有信号。(全为1)
int sigfillset(sigset_t *__set) {
  __set->sig = ~0;
  return 0;
}

// 向__set信号集中添加一个信号__signo。如果操作成功，__set将包含该信号。(置位为1)
int sigaddset(sigset_t *__set, int __signo) {
  if (!isValidSig(__signo)) {
    return -1;
  }
  __set->sig |= GET_SIG(__signo);
  return 0;
}

// 从__set信号集中删除一个信号__signo。如果操作成功，__set将不再包含该信号。(置位为0)
int sigdelset(sigset_t *__set, int __signo) {
  if (!isValidSig(__signo)) {
    return -1;
  }
  __set->sig &= ~GET_SIG(__signo);
  return 0;
}

// 检查信号__signo是否是__set信号集的成员。如果是，返回1；如果不是，返回0。
int sigismember(const sigset_t *__set, int __signo) {
  if (!isValidSig(__signo)) {
    return -1;
  }
  return (__set->sig & GET_SIG(__signo)) == 0 ? 0 : 1;
}

// 检查信号集__set是否为空。如果为空，返回1；如果不为空，返回0。
int sigisemptyset(const sigset_t *__set) {
  return __set->sig == 0;
}

// 计算两个信号集__left和__right的交集，并将结果存储在__set中。
int sigandset(sigset_t *__set, const sigset_t *__left, const sigset_t *__right) {
  __set->sig = __left->sig & __right->sig;
  return 0;
}

// 计算两个信号集__left和__right的并集，并将结果存储在__set中。
int sigorset(sigset_t *__set, const sigset_t *__left, const sigset_t *__right) {
  __set->sig = __left->sig | __right->sig;
  return 0;
}

// 根据__how的值**更改当前进程**的信号屏蔽字
// __set是要应用的新掩码，__oset（如果非NULL）则保存旧的信号屏蔽字
// __how可以是SIG_BLOCK（添加__set到当前掩码）、SIG_UNBLOCK（从当前掩码中移除__set）、或SIG_SETMASK（设置当前掩码为__set）。
int sigprocmask(int __how, const sigset_t * __set, sigset_t * __oset) {
  return syscall_set_sig_shield(__how, __set, __oset);
}

// 获取当前被阻塞且未处理的信号集，并将其存储在__set中。
int sigpending(sigset_t *__set) {
  return syscall_get_sig_pending(__set);
}