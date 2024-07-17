#include <lib.h>

int main() {
  int fd, n, r;
  char buffer[512 + 1];

  // 读取motd（message of today）文件并输出其内容
  debugf("icode: open /motd\n");
  if ((fd = open("/motd", O_RDONLY)) < 0) {
    user_panic("icode: open /motd: %d", fd);
  }

  debugf("icode: read /motd\n");
  while ((n = read(fd, buffer, sizeof buffer - 1)) > 0) {
    buffer[n] = 0;
    debugf("%s\n", buffer);
  }

  debugf("icode: close /motd\n");
  close(fd);

  // 调用spawnl函数执行文件init.b
  debugf("icode: spawn /init\n");
  if ((r = spawnl("init.b", "init", "initarg1", "initarg2", NULL)) < 0) {
    user_panic("icode: spawn /init: %d", r);
  }

  debugf("icode: exiting\n");
  return 0;
}
