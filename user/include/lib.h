#ifndef LIB_H
#define LIB_H
#include <args.h>
#include <env.h>
#include <fd.h>
#include <mmu.h>
#include <pmap.h>
#include <syscall.h>
#include <trap.h>

// 暴露给用户态的页表的起始地址，只有可读权限
#define vpt ((const volatile Pte *)UVPT)
// 暴露给用户态的页目录的起始地址，只有可读权限
#define vpd ((const volatile Pde *)(UVPT + (PDX(UVPT) << PGSHIFT)))
// 暴露给用户态的envs：获取Envs进程控制块管理数组的起始地址
#define envs ((const volatile struct Env *)UENVS)
// 获取页控制块管理数组的起始地址
#define pages ((const volatile struct Page *)UPAGES)

// libos
void exit(void) __attribute__((noreturn));

extern const volatile struct Env *env;

#define USED(x) (void)(x)

// debugf
void debugf(const char *fmt, ...);

void _user_panic(const char *, int, const char *, ...) __attribute__((noreturn));
void _user_halt(const char *, int, const char *, ...) __attribute__((noreturn));

#define user_panic(...) _user_panic(__FILE__, __LINE__, __VA_ARGS__)
#define user_halt(...) _user_halt(__FILE__, __LINE__, __VA_ARGS__)

#undef panic_on
#define panic_on(expr) \
  do { \
    int r = (expr); \
    if (r != 0) { \
      user_panic("'" #expr "' returned %d", r); \
    } \
  } while (0)

/// fork, spawn
int spawn(char *prog, char **argv);
int spawnl(char *prot, char *args, ...);
int fork(void);
int set_sigaction_entry(void);

/// syscalls
extern int msyscall(int, ...);

void syscall_putchar(int ch);
int syscall_print_cons(const void *str, u_int num);
u_int syscall_getenvid(void);
void syscall_yield(void);
int syscall_env_destroy(u_int envid);
int syscall_set_tlb_mod_entry(u_int envid, void (*func)(struct Trapframe *));
int syscall_mem_alloc(u_int envid, void *va, u_int perm);
int syscall_mem_map(u_int srcid, void *srcva, u_int dstid, void *dstva, u_int perm);
int syscall_mem_unmap(u_int envid, void *va);

// sigaction
int syscall_kill(u_int envid, int sig);
int syscall_sigaction(int signum, struct sigaction *new_act, struct sigaction *old_act);
int syscall_set_sig_shield(int how, sigset_t *new_set, sigset_t *old_set);
int syscall_get_sig_pending(sigset_t *set);
int syscall_set_sig_entry(u_int envid, u_int func_address);
int syscall_sig_finish(u_int sig_no);

// 创建新进程：在内核级
// 设置为内联函数，不会被编译为一个函数，而是直接内联展开在fork函数内，不会修改栈帧
__attribute__((always_inline)) inline static int syscall_exofork(void) {
  return msyscall(SYS_exofork, 0, 0, 0, 0, 0);
}

int syscall_set_env_status(u_int envid, u_int status);
int syscall_set_trapframe(u_int envid, struct Trapframe *tf);
void syscall_panic(const char *msg) __attribute__((noreturn));
int syscall_ipc_try_send(u_int envid, u_int value, const void *srcva, u_int perm);
int syscall_ipc_recv(void *dstva);
int syscall_cgetc(void);
int syscall_write_dev(void *va, u_int dev, u_int len);
int syscall_read_dev(void *va, u_int dev, u_int len);

// ipc.c
void ipc_send(u_int whom, u_int val, const void *srcva, u_int perm);
u_int ipc_recv(u_int *whom, void *dstva, u_int *perm);

// wait.c
void wait(u_int envid);

// console.c
int opencons(void);
int iscons(int fdnum);

// pipe.c
int pipe(int pfd[2]);
int pipe_is_closed(int fdnum);

// pageref.c
int pageref(void *);

// fprintf.c
int fprintf(int fd, const char *fmt, ...);
int printf(const char *fmt, ...);

// fsipc.c
int fsipc_open(const char *, u_int, struct Fd *);
int fsipc_map(u_int, u_int, void *);
int fsipc_set_size(u_int, u_int);
int fsipc_close(u_int);
int fsipc_dirty(u_int, u_int);
int fsipc_remove(const char *);
int fsipc_sync(void);
int fsipc_incref(u_int);

// fd.c
int close(int fd);
int read(int fd, void *buf, u_int nbytes);
int write(int fd, const void *buf, u_int nbytes);
int seek(int fd, u_int offset);
void close_all(void);
int readn(int fd, void *buf, u_int nbytes);
int dup(int oldfd, int newfd);
int fstat(int fdnum, struct Stat *stat);
int stat(const char *path, struct Stat *);

// file.c
int open(const char *path, int mode);
int read_map(int fd, u_int offset, void **blk);
int remove(const char *path);
int ftruncate(int fd, u_int size);
int sync(void);

#define user_assert(x) \
  do { \
    if (!(x)) \
      user_panic("assertion failed: %s", #x); \
  } while (0)

// File open modes
// 只读模式
#define O_RDONLY 0x0000
// 只写模式
#define O_WRONLY 0x0001
// 读写模式
#define O_RDWR 0x0002
// 全模式：用于匹配模式
#define O_ACCMODE 0x0003
// 不存在则创建模式
#define O_CREAT 0x0100
// 所见到0长度
#define O_TRUNC 0x0200

// Unimplemented open modes
#define O_EXCL 0x0400  /* error if already exists */
#define O_MKDIR 0x0800 /* create directory, not regular file */

// sigaction.c
// 信号注册函数
int sigaction(int signum, const struct sigaction *newact, struct sigaction *oldact);

// 信号发送函数
int kill(u_int envid, int sig);

/*
编号更小的信号拥有更高的优先级
- SIGINT  2	  中断信号	      停止进程
- SIGILL  4	  非法指令	      停止进程
- SIGKILL 9	  停止进程信号	  强制停止该进程，不可被阻塞
- SIGSEGV 11  访问地址错误，当访问[0, 0x003f_e000)内地址时	停止进程
- SIGCHLD 17  子进程终止信号  忽略
- SIGSYS  31  系统调用号未定义  忽略
*/

// 信号注册函数：设置某信号的处理函数
// 要操作的信号  要设置的对信号的新处理方式  原来对信号的处理方式
// 成功返回0，失败返回-1
int sigaction(int signum, const struct sigaction *newact, struct sigaction *oldact);

// 信号发送函数
// 当envid为0时，代表向自身发送信号
// 当envid对应进程不存在，或者sig不符合定义范围时，返回异常码-1。
int kill(u_int envid, int sig);

// 清空参数中的__set掩码，初始化信号集以排除所有信号。这意味着__set将不包含任何信号。(清0)
int sigemptyset(sigset_t *__set);

// 将参数中的__set掩码填满，使其包含所有已定义的信号。这意味着__set将包括所有信号。(全为1)
int sigfillset(sigset_t *__set);

// 向__set信号集中添加一个信号__signo。如果操作成功，__set将包含该信号。(置位为1)
int sigaddset(sigset_t *__set, int __signo);

// 从__set信号集中删除一个信号__signo。如果操作成功，__set将不再包含该信号。(置位为0)
int sigdelset(sigset_t *__set, int __signo);

// 检查信号__signo是否是__set信号集的成员。如果是，返回1；如果不是，返回0。
int sigismember(const sigset_t *__set, int __signo);

// 检查信号集__set是否为空。如果为空，返回1；如果不为空，返回0。
int sigisemptyset(const sigset_t *__set);

// 计算两个信号集__left和__right的交集，并将结果存储在__set中。
int sigandset(sigset_t *__set, const sigset_t *__left, const sigset_t *__right);

// 计算两个信号集__left和__right的并集，并将结果存储在__set中。
int sigorset(sigset_t *__set, const sigset_t *__left, const sigset_t *__right);

// 根据__how的值更改当前进程的信号屏蔽字。
// __set是要应用的新掩码，__oset（如果非NULL）则保存旧的信号屏蔽字。
//__how可以是SIG_BLOCK（添加__set到当前掩码）、SIG_UNBLOCK（从当前掩码中移除__set）、或SIG_SETMASK（设置当前掩码为__set）。
int sigprocmask(int __how, const sigset_t * __set, sigset_t * __oset);

// 获取当前被阻塞且未处理的信号集，并将其存储在__set中。
int sigpending(sigset_t *__set);


#endif
