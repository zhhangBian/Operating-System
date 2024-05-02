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
  // the value sent to us
  u_int env_ipc_value;
  // 发送方进程id
  u_int env_ipc_from;
  // whether this env is blocked receiving
  // 1：等待接受数据中；0：不可接受数据
  u_int env_ipc_recving;
  // 接收到的页面需要与自身的哪个虚拟页面完成映射
  // va at which the received page should be mapped
  u_int env_ipc_dstva;
  // 传递的页面的权限位设置
  // perm in which the received page should be mapped
  u_int env_ipc_perm;

  // 存储用户异常处理函数的地址
  // userspace TLB Mod handler
  u_int env_user_tlb_mod_entry;

  // Lab 6 scheduler counts
  u_int env_runs; // number of times we've been env_run'ed
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
#define ENV_CREATE_PRIORITY(name, priority) \
  ({ \
    extern u_char binary_##name##_start[]; \
    extern u_int binary_##name##_size; \
    env_create(binary_##name##_start, (u_int)binary_##name##_size, priority); \
  })

#define ENV_CREATE(name) \
  ({ \
    extern u_char binary_##name##_start[]; \
    extern u_int binary_##name##_size; \
    env_create(binary_##name##_start, (u_int)binary_##name##_size, 1); \
  })

#endif // !_ENV_H_
