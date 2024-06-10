#include <env.h>
#include <lib.h>
#include <mmu.h>
#define debug 0

static int pipe_close(struct Fd *);
static int pipe_read(struct Fd *fd, void *buf, u_int n, u_int offset);
static int pipe_stat(struct Fd *, struct Stat *);
static int pipe_write(struct Fd *fd, const void *buf, u_int n, u_int offset);

// 将管道也视作一种设备，配置相关的属性和方法
struct Dev devpipe = {
  .dev_id = 'p',
  .dev_name = "pipe",
  // 配置对设备的操作函数
  .dev_read = pipe_read,
  .dev_write = pipe_write,
  .dev_close = pipe_close,
  .dev_stat = pipe_stat,
};

// 管道的缓冲区大小
#define PIPE_SIZE 32

struct Pipe {
  // 下一个将要从管道读数据的位置：只有写者可更新
  u_int p_rpos;
  // 下一个将要向管道写数据的位置：只有读者可更新
  u_int p_wpos;
  // 数据缓冲区，类似于环形缓冲区的效果
  // - 当 p_rpos >= p_wpos 时，应该进程切换到写者运行
  // - 必须得在 p_wpos - p_rpos < PIPE_SIZE 时方可运行，否则要一直挂起
  u_char p_buf[PIPE_SIZE];
};

/* Overview:
 *   Create a pipe.
 *
 * Post-Condition:
 *   Return 0 and set 'pfd[0]' to the read end and 'pfd[1]' to the
 *   write end of the pipe on success.
 *   Return an corresponding error code on error.
 */
// 创建一个管道，将传入的指针设置为对应的文件描述符id
// fd0对应读端，fd1对应写端
int pipe(int fd_id[2]) {
  struct Fd *fd0, *fd1;
  void *fd_data_va;
  int func_info;

  // 申请两个文件描述符
  if ((func_info = fd_alloc(&fd0)) < 0 ||
      (func_info = syscall_mem_alloc(0, fd0, PTE_D | PTE_LIBRARY)) < 0) {
    goto err;
  }

  if ((func_info = fd_alloc(&fd1)) < 0 ||
      (func_info = syscall_mem_alloc(0, fd1, PTE_D | PTE_LIBRARY)) < 0) {
    goto err1;
  }

  // 设置管道的共享空间
  fd_data_va = fd2data(fd0);
  if ((func_info = syscall_mem_alloc(0, fd_data_va, PTE_D | PTE_LIBRARY)) < 0) {
    goto err2;
  }
  if ((func_info = syscall_mem_map(0, fd_data_va, 0, fd2data(fd1), PTE_D | PTE_LIBRARY)) < 0) {
    goto err3;
  }

  // 设置文件描述符对应的管道的属性
  fd0->fd_dev_id = devpipe.dev_id;
  fd0->fd_omode = O_RDONLY;

  fd1->fd_dev_id = devpipe.dev_id;
  fd1->fd_omode = O_WRONLY;

  debugf("[%08x] pipecreate \n", env->env_id, vpt[VPN(fd_data_va)]);

  // 将文件描述符id保存到传入的指针
  // 对应读端
  fd_id[0] = fd2num(fd0);
  // 对应写端
  fd_id[1] = fd2num(fd1);
  return 0;

// 集体的异常处理段
// 解除为管道分配的空间
err3:
  syscall_mem_unmap(0, (void *)fd_data_va);
// 解除分配的文件描述符fd1
err2:
  syscall_mem_unmap(0, fd1);
// 解除分配的文件描述符fd0
err1:
  syscall_mem_unmap(0, fd0);
// 返回错误信息
err:
  return func_info;
}

/* Overview:
 *   Check if the pipe is closed.
 *
 * Post-Condition:
 *   Return 1 if pipe is closed.
 *   Return 0 if pipe isn't closed.
 */
// 检查管道对应的端口是否已经关闭
// 写端关闭当且仅当pageref(p[0]) == pageref(pipe);
// 读端关闭当且仅当pageref(p[1]) == pageref(pipe);
static int _pipe_is_closed(struct Fd *fd, struct Pipe *pipe) {
  int fd_ref, pipe_ref, runs;
  do {
    runs = env->env_runs;
    // 得到对应的物理页引用次数，相等说明管道已经关闭
    // pageref操作不是原子的，两个引用次数可能不是同时完成的
    // 确保两次获取之间没有进程切换
    fd_ref = pageref(fd);
    pipe_ref = pageref(pipe);
  } while (runs != env->env_runs);

  return fd_ref == pipe_ref;
}

/* Overview:
 *   Read at most 'n' bytes from the pipe referred by 'fd' into 'vbuf'.
 *
 * Post-Condition:
 *   Return the number of bytes read from the pipe.
 *   The return value must be greater than 0, unless the pipe is closed and nothing
 *   has been written since the last read.
 */
// 从管道中读取n个字节
static int pipe_read(struct Fd *fd, void *buffer_va, u_int n, u_int offset) {
  // 获取fd对应的管道
  struct Pipe *pipe = fd2data(fd);
  // 利用char *是为了确保每次只写入一个字节
  char *read_buffer = (char *)buffer_va;

  // 读取n个字节
  // 由于管道设计并发操作，等待写端大于读端，并且管道未关闭
  for (int i = 0; i < n; i++) {
    while (pipe->p_rpos >= pipe->p_wpos) {
      // 如果已经有字符读入，但现在管道为空（表现为写端小于读端），返回i
      if (i > 0 || _pipe_is_closed(fd, pipe)) {
        return i;
      } 
      // 等待写端写入
      else {
        syscall_yield();
      }
    }
    // 从管道中读取数据
    read_buffer[i] = pipe->p_buf[pipe->p_rpos % PIPE_SIZE];
    // 将写端后移
    pipe->p_rpos++;
  }
}

/* Overview:
 *   Write 'n' bytes from 'vbuf' to the pipe referred by 'fd'.
 *
 * Post-Condition:
 *   Return the number of bytes written into the pipe.
 */
// 向管道中写入n个字节
// offest参数实际上没有使用
static int pipe_write(struct Fd *fd, const void *buffer, u_int n, u_int offset) {
  // 获取fd对应的管道
  struct Pipe *pipe = fd2data(fd);
  // 利用char *是为了确保每次只写入一个字节
  char *write_buffer = (char *)buffer;

  // 写入n个字节
  for (int i = 0; i < n; i++) {
    // 如管道缓冲区已满：等待读入
    while (pipe->p_wpos - pipe->p_rpos >= PIPE_SIZE) {
      // 如果管道已经关闭
      if (_pipe_is_closed(fd, pipe)) {
        return i;
      } 
      // 等待读数据，腾出缓冲区
      else {
        syscall_yield();
      }
    }
    // 写入数据
    pipe->p_buf[pipe->p_wpos % PIPE_SIZE] = write_buffer[i];
    pipe->p_wpos++;
  }

  return n;
}

/* Overview:
 *   Check if the pipe referred by 'fdnum' is closed.
 *
 * Post-Condition:
 *   Return 1 if the pipe is closed.
 *   Return 0 if the pipe isn't closed.
 *   Return -E_INVAL if 'fdnum' is invalid or unmapped.
 */
// 检查文件描述符对应的管道是否关闭
int pipe_is_closed(int fd_no) {
  struct Pipe *pipe;
  struct Fd *fd;
  int func_info;

  // 获取对应的文件描述符
  if ((func_info = fd_lookup(fd_no, &fd)) < 0) {
    return func_info;
  }
  // 获取管道
  pipe = (struct Pipe *)fd2data(fd);
  // 判断是否关闭
  return _pipe_is_closed(fd, pipe);
}

/* Overview:
 *   Close the pipe referred by 'fd'.
 *
 * Post-Condition:
 *   Return 0 on success.
 */
// 关闭管道
// 本质是解除文件描述符和管道数据的内存映射
static int pipe_close(struct Fd *fd) {
  void *va = (void *)fd2data(fd);
  syscall_mem_unmap(0, fd);
  syscall_mem_unmap(0, va);
  return 0;
}

// 获取设备函数，但此处无用
static int pipe_stat(struct Fd *fd, struct Stat *stat) {
  return 0;
}
