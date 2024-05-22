#include <env.h>
#include <lib.h>
#include <mmu.h>

void exit(void) {
  // After fs is ready (lab5), all our open files should be closed before dying.
#if !defined(LAB) || LAB >= 5
  close_all();
#endif

  syscall_env_destroy(0);
  user_panic("unreachable code");
}

// 当前进程的进程控制块，可以通过env获取当前进程的信息
const volatile struct Env *env;

extern int main(int, char **);

void libmain(int argc, char **argv) {
  // 用户程序 在运行时入口会将一个 用户空间中 的指针变量struct Env *env 指向当前进程的控制块
  // 允许进程（在用户态）访问自身的进程控制块
  env = &envs[ENVX(syscall_getenvid())];

  // call user main routine
  main(argc, argv);

  // exit gracefully
  exit();
}
