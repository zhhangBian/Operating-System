#include <env.h>
#include <lib.h>
#include <mmu.h>
#include <syscall.h>
#include <trap.h>

// 此文件中的函数与内核中的系统调用函数是一一对应的
// 是在用户空间最接近的**内核函数**

// 对于mysycall(type,...)函数，第一个参数为系统调用号
// 支持可变长参数，最多有6个参数，是传递给内核的参数
// 在这里，负责填入相关的参数等待syscall：由msyscall调用

void syscall_putchar(int ch) {
  msyscall(SYS_putchar, ch);
}

int syscall_print_cons(const void *str, u_int num) {
  // 调用内核函数，由此陷入内核态
  return msyscall(SYS_print_cons, str, num);
}

// 返回当前进程的envid
u_int syscall_getenvid(void) {
  return msyscall(SYS_getenvid);
}

// 实现用户进程对CPU的放弃，强制切换进程，从而调度其他的进程
void syscall_yield(void) {
  msyscall(SYS_yield);
}

int syscall_env_destroy(u_int envid) {
  return msyscall(SYS_env_destroy, envid);
}

int syscall_set_tlb_mod_entry(u_int envid, void (*func)(struct Trapframe *)) {
  return msyscall(SYS_set_tlb_mod_entry, envid, func);
}

// 分配内存：给该程序所允许的虚拟内存空间显式地分配实际的物理内存
int syscall_mem_alloc(u_int envid, void *va, u_int perm) {
  return msyscall(SYS_mem_alloc, envid, va, perm);
}

// 共享物理页面，将源进程和目标进程  的相应内存映射到相同的相应地址
int syscall_mem_map(u_int srcid, void *srcva, u_int dstid, void *dstva, u_int perm) {
  return msyscall(SYS_mem_map, srcid, srcva, dstid, dstva, perm);
}

int syscall_mem_unmap(u_int envid, void *va) {
  return msyscall(SYS_mem_unmap, envid, va);
}

int syscall_set_env_status(u_int envid, u_int status) {
  return msyscall(SYS_set_env_status, envid, status);
}

int syscall_set_trapframe(u_int envid, struct Trapframe *tf) {
  return msyscall(SYS_set_trapframe, envid, tf);
}

void syscall_panic(const char *msg) {
  int r = msyscall(SYS_panic, msg);
  user_panic("SYS_panic returned %d", r);
}

int syscall_ipc_try_send(u_int envid, u_int value, const void *srcva, u_int perm) {
  return msyscall(SYS_ipc_try_send, envid, value, srcva, perm);
}

int syscall_ipc_recv(void *dstva) {
  return msyscall(SYS_ipc_recv, dstva);
}

int syscall_cgetc() {
  return msyscall(SYS_cgetc);
}

int syscall_write_dev(void *va, u_int dev, u_int size) {
  /* Exercise 5.2: Your code here. (1/2) */

}

int syscall_read_dev(void *va, u_int dev, u_int size) {
  /* Exercise 5.2: Your code here. (2/2) */

}
