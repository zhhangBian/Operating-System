#ifndef _ENV_H_
#define _ENV_H_

#include <mmu.h>
#include <queue.h>
#include <trap.h>
#include <types.h>

#define LOG2NENV 10
// 进程块的数量
#define NENV (1 << LOG2NENV)
// 获取envid对应的进程控制块地址
#define ENVX(envid) ((envid) & (NENV - 1))

// All possible values of 'env_status' in 'struct Env'.
#define ENV_FREE 0
#define ENV_RUNNABLE 1
#define ENV_NOT_RUNNABLE 2

// 中断信号
#define SIGINT 2
// 非法指令
#define SIGILL 4
// 停止进程信号
#define SIGKILL 9
// 访问地址错误，当访问[0, 0x003f_e000)内地址时
#define SIGSEGV 11
// 子进程终止信号
#define SIGCHLD 17
// 系统调用号未定义
#define SIGSYS 31

/* Values for the HOW argument to `sigprocmask'.  */
#define	SIG_BLOCK     0
#define	SIG_UNBLOCK   1
#define	SIG_SETMASK   2

#define GET_SIG(sig) (1 << (sig - 1)) 

// 信号掩码结构体
typedef struct sigset_t {
  // 表示MOS所需要处理的[1,32]信号掩码
  // 对应位为1表示阻塞，0表示未阻塞
  uint32_t sig;
} sigset_t;

struct sigaction {
  // 信号的处理函数，当未屏蔽信号到达并且“处理时机”合适时，进程就会执行该函数
  void (*sa_handler)(int);
  // 存放了 对应信号处理函数 被执行时 需要被阻塞的信号掩码
  sigset_t sa_mask;
};

// Control block of an environment (process).
// Env就是PCB，PCB是系统感知进程存在的唯一标志。进程与PCB 是一一对应的。
struct Env {
  // 当前进程的的上下文环境：GRF+CP0
  // 发生进程调度、陷入内核时保存
  struct Trapframe env_tf;

  // 构造空闲链表的指针域
  LIST_ENTRY(Env) env_link;

  // 进程id，唯一
  u_int env_id;
  // 进程的ASID，用于TLB识别相应的虚拟地址，唯一
  u_int env_asid;
  // 进程的父进程的id
  u_int env_parent_id;

  // 记录进程的状态
  // - ENV_FREE : 进程控制块没有被任何进程使用，处于进程空闲链表中。
  // - ENV_NOT_RUNNABLE : 处于阻塞状态
  // - ENV_RUNNABLE : 该进程处于执行状态或就绪状态，即其可能是正在运行的，也可能正在等待被调度。
  u_int env_status;

  // 该进程的页目录地址（虚拟地址）
  Pde *env_pgdir;

  // 构造调度链表的指针域
  TAILQ_ENTRY(Env) env_sched_link; // intrusive entry in 'env_sched_list'

  // 进程优先级
  u_int env_pri;

  // 用于进程间通信
  // 所有的进程都共享同一个内核空间（主要为kseg0）
  // 在不同空间之间交换数据，可以借助于内核空间来实现。
  // 发送方进程可以将数据以系统调用的形式存放在进程控制块中
  // 接收方进程同样以系统调用的方式在进程控制块中找到对应的数据，读取并返回。

  // 进程传递的具体数值
  u_int env_ipc_value;
  // 发送方进程id
  u_int env_ipc_from;
  // 握手信号：1：等待接受数据中；0：不可接受数据
  u_int env_ipc_recving;
  // 接收到的页面需要与自身的哪个虚拟页面完成映射
  u_int env_ipc_dstva;
  // 接受的页面的权限位设置
  u_int env_ipc_perm;

  // 存储用户态 TLB Mod异常的处理函数的地址
  // mod: modify，写入异常，对应写入不可写页面时产生该异常
  u_int env_user_tlb_mod_entry;

  // 进程运行的时间片数
  u_int env_runs;

  // sigaction
  // 当前正在处理的信号
  u_int sig_now;
  // 存储进程接收到的信号列表
  uint32_t sig_to_handle;

  // 信号处理函数，在用户态
  u_int sig_entry;
  // 相应的信号处理函数
  struct sigaction act[64];
  // 掩码栈
  int sig_mask_pos;
  uint32_t sig_mask_stack[32];
};

LIST_HEAD(Env_list, Env);
TAILQ_HEAD(Env_sched_list, Env);
extern struct Env *curenv;		     // the current env
extern struct Env_sched_list env_sched_list; // runnable env list

void env_init(void);
int env_alloc(struct Env **e, u_int parent_id);
void env_free(struct Env *);
struct Env *env_create(const void *binary, size_t size, int priority);
void env_destroy(struct Env *e);

int envid2env(u_int envid, struct Env **penv, int checkperm);
void env_run(struct Env *e) __attribute__((noreturn));

void env_check(void);
void envid2env_check(void);

// 使用宏拼接指令，在C中较难以函数方法实现
// 创建一个控制块，并指定优先级
#define ENV_CREATE_PRIORITY(name, priority) \
  ({ \
    extern u_char binary_##name##_start[]; \
    extern u_int binary_##name##_size; \
    env_create(binary_##name##_start, (u_int)binary_##name##_size, priority); \
  })

// 创建一个控制块，但不指定优先级，默认为1
#define ENV_CREATE(name) \
  ({ \
    extern u_char binary_##name##_start[]; \
    extern u_int binary_##name##_size; \
    env_create(binary_##name##_start, (u_int)binary_##name##_size, 1); \
  })

#endif // !_ENV_H_
