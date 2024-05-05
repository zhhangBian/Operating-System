// User-level IPC library routines

#include <env.h>
#include <lib.h>
#include <mmu.h>

// Send val to whom.
// This function keeps trying until it succeeds.
// It should panic() on any error other than -E_IPC_NOT_RECV.
// Hint: use syscall_yield() to be CPU-friendly.
// 会大量使用srcva为0的调用来表示只传value值，而不需要传递物理页面，
void ipc_send(u_int receive_id, u_int value, const void *src_va, u_int perm) {
  int func_info;
  // 轮询直至接受到信息
  while ((func_info = syscall_ipc_try_send(receive_id, value, src_va, perm)) == -E_IPC_NOT_RECV) {
    // 使用yield让出CPU，避免无意义轮询
    syscall_yield();
  }
  user_assert(func_info == 0);
}

// Receive a value.  
// Return the value and store the caller's envid in *whom.
//
// Hint: use env to discover the value and who sent it.
u_int ipc_recv(u_int *whom, void *dst_va, u_int *perm) {
  int func_info = syscall_ipc_recv(dst_va);
  if (func_info != 0) {
    user_panic("syscall_ipc_recv err: %d", func_info);
  }

  if (whom) {
    *whom = env->env_ipc_from;
  }

  if (perm) {
    *perm = env->env_ipc_perm;
  }

  return env->env_ipc_value;
}
