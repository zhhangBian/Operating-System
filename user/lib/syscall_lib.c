#include <env.h>
#include <lib.h>
#include <mmu.h>
#include <syscall.h>
#include <trap.h>

// 在用户空间提供给用户的系统调用函数，最接近内核函数的函数，在调用这里的函数后，开始陷入内核态
// 此文件中的函数与内核中的系统调用函数是一一对应的

// 对于mysycall(type,...)函数，第一个参数为系统调用号
// 支持可变长参数，最多有6个参数，是传递给内核的参数
// 在这里，负责填入相关的参数等待syscall：由msyscall调用
// msyscall函数见 syacall_wrap.S

// 打印一个字符，是所有打印的基础
void syscall_putchar(int ch) {
  msyscall(SYS_putchar, ch);
}

// 向终端打印一个字符
int syscall_print_cons(const void *str, u_int num) {
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

// 终止参数envid的进程，由当前进程进行终止
int syscall_env_destroy(u_int envid) {
  return msyscall(SYS_env_destroy, envid);
}

// 注册进程自身的页写入异常处理函数
int syscall_set_tlb_mod_entry(u_int envid, void (*func)(struct Trapframe *)) {
  return msyscall(SYS_set_tlb_mod_entry, envid, func);
}

// 分配内存：给该程序所允许的虚拟内存空间显式地分配实际的物理内存
int syscall_mem_alloc(u_int envid, void *va, u_int perm) {
  return msyscall(SYS_mem_alloc, envid, va, perm);
}

// 共享物理页面，将目标进程的虚拟地址映射到  源进程虚拟地址对应的物理内存
int syscall_mem_map(u_int srcid, void *srcva, u_int dstid, void *dstva, u_int perm) {
  return msyscall(SYS_mem_map, srcid, srcva, dstid, dstva, perm);
}

// 解除映射关系
int syscall_mem_unmap(u_int envid, void *va) {
  return msyscall(SYS_mem_unmap, envid, va);
}

// 根据设定的状态将进程加入或移除调度队列
int syscall_set_env_status(u_int envid, u_int status) {
  return msyscall(SYS_set_env_status, envid, status);
}

// 将参数envid对应的进程的栈帧设置为当前的异常栈
int syscall_set_trapframe(u_int envid, struct Trapframe *tf) {
  return msyscall(SYS_set_trapframe, envid, tf);
}

// 进程崩溃，释放信息
void syscall_panic(const char *msg) {
  int r = msyscall(SYS_panic, msg);
  user_panic("SYS_panic returned %d", r);
}

// 进程间通信尝试发送信息
int syscall_ipc_try_send(u_int envid, u_int value, const void *src_va, u_int perm) {
  return msyscall(SYS_ipc_try_send, envid, value, src_va, perm);
}

// 进程间通信接受信息
int syscall_ipc_recv(void *dst_va) {
  return msyscall(SYS_ipc_recv, dst_va);
}

// 读入一个字符，一切输入的起始
int syscall_cgetc() {
  return msyscall(SYS_cgetc);
}

// 向设备写入
int syscall_write_dev(void *data_addr, u_int device_addr, u_int data_len) {
  return msyscall(SYS_write_dev, data_addr, device_addr, data_len);
}

// 从设备读入
int syscall_read_dev(void *data_addr, u_int device_addr, u_int data_len) {
  return msyscall(SYS_read_dev, data_addr, device_addr, data_len);
}
