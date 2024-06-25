# 任务实现思路

对于`sigaction`，我将任务实现拆分为了如下几个部分：

1. 进程控制块`Env`支持信号处理的数据结构修改
2. 在进程调度时进入信号处理函数的修改
3. 具体的信号处理函数
   1. 任务要求的对于信号结构体的处理函数
   2. 信号具体含义所代表的执行函数

修改了11个文件，添加了若干函数达成了信号处理的效果。

## 数据结构修改

为了支持信号，在阅读知道书后发现：信号的主体依旧是进程：信号的发送、接收、处理的对象始终都是进程，对于进程处理函数的设置都是对于某一个特定进程的，不需要考虑跨进程设置全局处理函数的情况。

我对`Env`进程控制块进行了如下修改：

```c
struct Env {
  // ...
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
}
```

添加的变量的具体含义可见注释。在后续的代码书写中，发现其实`sig_now`也没有太大的用途，不需要在进程中保留当前处理的信号，但为了保留可能的扩展性，保留了此信息。

## 信号处理流程

主要参考了助教给出的信号处理流程图。

![信号处理流程](https://pigkiller-011955-1319328397.cos.ap-beijing.myqcloud.com/img/202406232108473.png)

我的信号处理流程为：

1. 信号发送`kill()`是一个用户态函数，会陷入内核调用内核态函数。在此步，通过获取目标进程，将其进程控制块中的`sig_to_handle`变量的对应位置位，达到了告知信号待处理的目的。

2. 在每次进程切换的过程，我向`genex.S`中的内核态向用户态函数中添加了向信号处理函数跳转的`do_sigaction()`，这是一个内核态处理函数，会在返回用户态前完成对信号的处理。

   1. 为了避免信号重入：为了保证操作的有序性，如果每次在内核态时都进行信号重入，那么会加大编程的复杂性。在不影响信号处理的基础上，只允许每次从内核态返回用户态时才允许信号处理

      ```c
      // 以上在内核态，不用管
      if(tf->cp0_epc >= ULIM || curenv->sig_to_handle == 0) {
        return;
      }
      ```

      通过此函数，达到了避免无脑信号重入的问题，完成了信号的有序处理

   2. 在信号处理阶段，首先根据掩码`sig_shield`（存储在进程控制块中）和当前待处理的信号，找出当前**可处理的**、**优先级最高的**信号，并将**相应的掩码入栈**，保证在信号处理的具体函数中的正确性。

      ```c
      // 设置相应的信号处理
      curenv->sig_now = sig_now;
      // 屏蔽当前处理的信号
      sig_shield |= GET_SIG(sig_now);
      sig_shield |= curenv->act[sig_now].sa_mask.sig;
      curenv->sig_mask_stack[++(curenv->sig_mask_pos)] = sig_shield;
      ```

      掩码栈的设计是在信号处理函数中设置了一个数组实现栈，但其实应该有更为精巧的设计。

   3. 接下来，需要跳转到信号对应的信号处理函数，需要保留处理信号前的栈帧。对此，我设置了一个**用户态的信号处理入口**`sig_entry()`，具体的设计参考了`fork()`中的`cow_entry()`，以实现在用户态实现较多的操作，将栈帧作为其一个参数，达成了完成信号处理后恢复栈的作用

      ```c
      // 保留原有的栈帧
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
      ```

      参数是手动填入的，但是也可以通过函数指针直接完成具体的函数形式，由编译器填入相关参数。

3. 在内核态设置完相关的信号处理状态，设置了跳转的用户态入口后，就要来到用户态处理阶段了。在用户态处理信号，主要是因为信号机制允许的用户自定义的信号处理函数是在用户态的。

   ```c
   static void __attribute__((noreturn)) sig_entry(struct Trapframe *tf,  void (*sig_handler)(int), int sig_no) { }
   ```

   1. 我参考`cow_wntry()`设置了一个不返回函数`sig_entry()`，其实也会有返回，不过这里的返回是广义上的，即恢复原有的栈帧，达成回到处理信号前的处理阶段。

   2. 由于相关的信号处理函数保存在进程控制块中，由内核态判断得到需要处理的信号后从进程控制块中取出，作为参数送往了`sig_entry()`中，在`sig_entry()`中只需要判断对应的处理函数时候有效，若无效则采用默认的处理函数。

      ```c
      // 如果有相应的处理函数
      if (sig_handler != 0) {
          sig_handler(sig_no); //直接调用定义好的处理函数
      } else {
          // 如果没有
          switch (sig_no) {
              case SIGINT:
              case SIGILL:
              case SIGKILL:
              case SIGSEGV:
                  exit();
                  break;
              default:
                  {}
          }
      }
      ```

      `sig_handler()`是用户态的处理函数，是由用户定义的，若为空则执行默认处理函数，大部分为`exit()`。

   3. 在完成信号处理后，需要修改进程控制块相关信息，告知信号已经处理完成，主要就是去除待处理信号中的相应置位、恢复掩码栈，以及恢复栈帧，通过两个系统调用完成。

      ```c
      syscall_sig_finish(sig_no);
      func_info = syscall_set_trapframe(0, tf);
      // 不返回函数返回就出错了
      user_panic("syscall_set_trapframe returned %d", func_info);
      ```

## 对内核的修改

将原先内核中会`panic`崩溃的情况修改为了发送对应相应的信号，实现了更为优雅的设计。

- 访问错误地址

  ```c
  static void passive_alloc(u_int va, Pde *pgdir, u_int asid) {
  	// ...
      if (va < UTEMP) {
          sys_kill(0, SIGSEGV);
          // panic("address too low");
      }
  }
  ```

- 错误系统调用

  ```c
  void do_reserved(struct Trapframe *tf) {
      print_tf(tf);
      int t = (tf->cp0_cause >> 2) & 0x1f;
      // 错误系统调用异常号为10
      if(t == 10) {
          sys_kill(0, SIGILL);
      }
      else {
          panic("Unknown ExcCode %2d", t);
      }
  }
  ```

- 杀死那个子进程：添加了向父进程发送信号的操作

  ```c
  void env_free(struct Env *env) {
      // ...
      if(env->env_parent_id) {
          sys_kill(env->env_parent_id, SIGCHLD);
      }
  }
  ```


## 一些实现细节

### `sig_entry()`的设置

`sig_entry()`是用户态信号处理的入口，其地址保存在每一个进程的进程控制块中，那么，地址在什么时候设置呢。

在`libmain()`函数中，我通过增加的函数`set_sigaction_entry()`完成了相应的设置。此函数处在用户态，可以获取到`sig_entry()`的地址。在此基础上，通过两个系统调用完成了对相应进程控制块的修改，完成了`sig_entry()`和`cow_entry()`的设置。

```c
int set_sigaction_entry(void) {
    try(syscall_set_sig_entry(0, sig_entry));
    try(syscall_set_tlb_mod_entry(0, cow_entry));
    return 0;
}
```

### `fork()`中的继承

我继承了父进程全部的信号处理信息，包括掩码栈、当前处理的信号等等。

但实际上由于我的当前处理信号并没有起到什么实质性的作用，而是由程序运行的逻辑保证当前处理的进程，一些继承的信号是有些冗余的。

### 任务要求的信号处理函数

这些函数定义在用户态，需要修改的信息保存在进程控制块中，通过添加相应的系统调用完成了对相应属性的修改。

```c
int sigaction(int signum, const struct sigaction *newact, struct sigaction *oldact) {
  if (!isValidSig(signum)) {
    return -1;
  }

  set_sigaction_entry();
  return syscall_sigaction(signum, newact, oldact);
}

int kill(u_int envid, int sig) {
  if (!isValidSig(sig)) {
    return -1;
  }

  return syscall_kill(envid, sig);
}

int sigprocmask(int __how, const sigset_t * __set, sigset_t * __oset) {
  return syscall_set_sig_shield(__how, __set, __oset);
}

int sigpending(sigset_t *__set) {
  return syscall_get_sig_pending(__set);
}
```

# 总结

实现了对与信号的处理。以一个回顾的视角来看，主要的工作在于设置正确的信号处理逻辑，以及添加部分系统调用。添加系统调用是纯粹的体力活，而设置信号处理逻辑需要对进程的切换、内核态和用户态由较为深入的理解，在这方面有了很大的收获。

以及一些对信号处理的边界情况需要考虑，这是需要注意的。
