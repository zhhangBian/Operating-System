// User-level IPC library routines
// 用户态的IPC函数
// 在此可以使用内核态暴露的envs和pages

#include <env.h>
#include <lib.h>
#include <mmu.h>






// Send val to whom.
// This function keeps trying until it succeeds.
// It should panic() on any error other than -E_IPC_NOT_RECV.
// Hint: use syscall_yield() to be CPU-friendly.
// 用户态的ipc_send函数，尝试发送信息直至成功
// 使用srcva为0的调用来表示只传value值，而不需要传递物理页面，
void ipc_send(u_int receive_id, u_int value, const void *src_va, u_int perm) {
  int func_info;
  // 轮询直至接受到信息
  // 信息的接受是由接受进程手动设置的，如果对方进程一直不接受，则会一直阻塞
  while ((func_info = syscall_ipc_try_send(receive_id, value, src_va, perm)) == -E_IPC_NOT_RECV) {
    // 使用yield让出CPU，避免无意义轮询
    syscall_yield();
  }
  user_assert(func_info == 0);
}

// Receive a value.
// Return the value and store the caller's envid in *send_id.
// 用户态的ipc_receive函数，本质上是读取对方发送后设置好的信息
u_int ipc_recv(u_int *send_id_pointer, void *dst_va, u_int *permission_pointer) {
  // 手动开始接受信息，调用系统调用，由此陷入内核态
  int func_info = syscall_ipc_recv(dst_va);
  if (func_info != 0) {
    user_panic("syscall_ipc_recv err: %d", func_info);
  }

  if (send_id_pointer) {
    *send_id_pointer = env->env_ipc_from;
  }

  if (permission_pointer) {
    *permission_pointer = env->env_ipc_perm;
  }

  return env->env_ipc_value;
}

void sem_open(int sem_id, int n) {
	syscall_sem_open(sem_id, n);
}

int sem_wait(int sem_id) {
	int r;
	// Lab 4-1-Exam: Your code here. (1/9)
	// Implement process blocking
	
	while((r = syscall_sem_wait(sem_id)) == -E_SEM_NOT_OPEN) {
		syscall_yield();
	}

	return r;
}

int sem_post(int sem_id) {
	return syscall_sem_post(sem_id);
}

int sem_kill(int sem_id) {
	return syscall_sem_kill(sem_id);
}

