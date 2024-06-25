#include <env.h>
#include <lib.h>
// 等待对应进程退出
void wait(u_int envid) {
  const volatile struct Env *e = &envs[ENVX(envid)];
  while (e->env_id == envid && e->env_status != ENV_FREE) {
    syscall_yield();
  }
}
