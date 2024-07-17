#include <lib.h>
#include <mmu.h>

static int cons_read(struct Fd *, void *, u_int, u_int);
static int cons_write(struct Fd *, const void *, u_int, u_int);
static int cons_close(struct Fd *);
static int cons_stat(struct Fd *, struct Stat *);

// 将终端申请为一种设备
struct Dev devcons = {
  .dev_id = 'c',
  .dev_name = "cons",
  .dev_read = cons_read,
  .dev_write = cons_write,
  .dev_close = cons_close,
  .dev_stat = cons_stat,
};

// 判断文件描述符id对应的是不是终端
int iscons(int fdnum) {
  struct Fd *fd;
  int func_info;

  if ((func_info = fd_lookup(fdnum, &fd)) < 0) {
    return func_info;
  }

  return fd->fd_dev_id == devcons.dev_id;
}

// 申请打开一个终端，返回对应的 文件描述符
int opencons(void) {
  struct Fd *fd;
  int func_info;
  // 申请一个文件描述符
  if ((func_info = fd_alloc(&fd)) < 0) {
    return func_info;
  }
  // 为文件描述符分配对应的空间
  if ((func_info = syscall_mem_alloc(0, fd, PTE_D | PTE_LIBRARY)) < 0) {
    return func_info;
  }
  // 配置终端的操作和属性
  fd->fd_dev_id = devcons.dev_id;
  fd->fd_omode = O_RDWR;

  return fd2num(fd);
}

// 从终端中读取n个字节到bbuffer，offest实际上没有用到
int cons_read(struct Fd *fd, void *buffer, u_int n, u_int offset) {
  int ch;

  if (n == 0) {
    return 0;
  }
  // 使用系统调用读入一个字符
  while ((ch = syscall_cgetc()) == 0) {
    syscall_yield();
  }

  if (ch != '\r') {
    debugf("%c", ch);
  } else {
    debugf("\n");
  }

  if (ch < 0) {
    return ch;
  }
  // 0x4为ctl-d，代表eof
  if (ch == 0x04) {
    return 0;
  }
  // 写入字符到buffer
  *(char *)buffer = ch;
  return 1;
}

// 向终端写入n个字节
int cons_write(struct Fd *fd, const void *buffer, u_int n, u_int offset) {
  int func_info;
  // 使用系统调用向终端打印一个字符
  if ((func_info = syscall_print_cons(buffer, n)) < 0) {
    return func_info;
  }
  return n;
}

// 关闭终端，在此处没有实际含义
int cons_close(struct Fd *fd) {
  return 0;
}

// 终端的状态
int cons_stat(struct Fd *fd, struct Stat *stat) {
  strcpy(stat->st_name, "<cons>");
  return 0;
}
