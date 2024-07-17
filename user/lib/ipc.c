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
