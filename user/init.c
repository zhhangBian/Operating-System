#include <lib.h>

struct {
  char msg1[5000];
  char msg2[1000];
} data = {"this is initialized data", "so is this"};

char bss[6000];

int sum(char *s, int n) {
  int tot = 0;
  for (int i = 0; i < n; i++) {
    tot ^= i * s[i];
  }
  return tot;
}

int main(int argc, char **argv) {
  int x, want;
  int func_info;

  debugf("init: running\n");
  // 测试ELF文件加载正确性的代码
  want = 0xf989e;
  if ((x = sum((char *)&data, sizeof data)) != want) {
    debugf("init: data is not initialized: got sum %08x wanted %08x\n", x, want);
  } else {
    debugf("init: data seems okay\n");
  }
  // 测试bss端是否被正确加载
  if ((x = sum(bss, sizeof bss)) != 0) {
    debugf("bss is not initialized: wanted sum 0 got %08x\n", x);
  } else {
    debugf("init: bss seems okay\n");
  }
  // 测试参数段是否被正确加载
  debugf("init: args:");
  for (int i = 0; i < argc; i++) {
    debugf(" '%s'", argv[i]);
  }
  debugf("\n");

  debugf("init: running sh\n");


  // 打开一个终端文件，设置为stdin
  // 由于之前没有打开过，文件描述符id必然为0
  if ((func_info = opencons()) != 0) {
    user_panic("opencons: %d", func_info);
  }
  // 复制一个文件描述符设置为stdout，都是对终端的读写
  // 文件描述符id必然为1
  if ((func_info = dup(0, 1)) < 0) {
    user_panic("dup: %d", func_info);
  }
  // 创建shell进程
  int shell_env_id;
  while (1) {
    debugf("init: starting sh\n");
    // 创建一个shell进程
    shell_env_id = spawnl("sh.b", "sh", NULL);
    if (shell_env_id < 0) {
      debugf("init: spawn sh: %d\n", shell_env_id);
      return shell_env_id;
    }
    // 等待shell进程退出，退出后再创建一个
    wait(shell_env_id);
  }
}
