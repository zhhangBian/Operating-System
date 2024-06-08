#include <env.h>
#include <lib.h>
#include <mmu.h>
#define debug 0

static int pipe_close(struct Fd *);
static int pipe_read(struct Fd *fd, void *buf, u_int n, u_int offset);
static int pipe_stat(struct Fd *, struct Stat *);
static int pipe_write(struct Fd *fd, const void *buf, u_int n, u_int offset);

struct Dev devpipe = {
  .dev_id = 'p',
  .dev_name = "pipe",
  .dev_read = pipe_read,
  .dev_write = pipe_write,
  .dev_close = pipe_close,
  .dev_stat = pipe_stat,
};

#define PIPE_SIZE 32 // small to provoke races

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
int pipe(int pfd[2]) {
  struct Fd *fd0, *fd1;
  void *va;
  int func_info;

  // 分配两个文件描述符
  // 一个代表只读端，一个代表只写端
  if ((func_info = fd_alloc(&fd0)) < 0 || 
      (func_info = syscall_mem_alloc(0, fd0, PTE_D | PTE_LIBRARY)) < 0) {
    return func_info;
  }

  if ((func_info = fd_alloc(&fd1)) < 0 || 
      (func_info = syscall_mem_alloc(0, fd1, PTE_D | PTE_LIBRARY)) < 0) {
    goto err1;
  }

  // 将文件描述符映射到同一内存区域
  va = fd2data(fd0);
  if ((func_info = syscall_mem_alloc(0, (void *)va, PTE_D | PTE_LIBRARY)) < 0) {
    goto err2;
  }
  // 将fd1对应的虚拟地址映射到同一物理内存
  if ((func_info = syscall_mem_map(0, (void *)va, 0, (void *)fd2data(fd1), PTE_D | PTE_LIBRARY)) < 0) {
    goto err3;
  }

  // 设置管道相应的设备属性
  fd0->fd_dev_id = devpipe.dev_id;
  fd0->fd_omode = O_RDONLY;

  fd1->fd_dev_id = devpipe.dev_id;
  fd1->fd_omode = O_WRONLY;

  debugf("[%08x] pipecreate \n", env->env_id, vpt[VPN(va)]);

  // 保存相应的文件描述符
  pfd[0] = fd2num(fd0);
  pfd[1] = fd2num(fd1);

  return 0;

// 相应的公共错误信息处理
// 取消映射
err3:
  syscall_mem_unmap(0, (void *)va);
// 取消fd1的映射
err2:
  syscall_mem_unmap(0, fd1);
// 取消fd0的映射
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
// 检查管道是否已经关闭
static int _pipe_is_closed(struct Fd *fd, struct Pipe *pipe) {
  // The 'pageref(p)' is the total number of readers and writers.
  // The 'pageref(fd)' is the number of envs with 'fd' open
  // (readers if fd is a reader, writers if fd is a writer).
  //
  // Check if the pipe is closed using 'pageref(fd)' and 'pageref(p)'.
  // If they're the same, the pipe is closed.
  // Otherwise, the pipe isn't closed.

  int fd_ref, pipe_ref, runs;
  // Use 'pageref' to get the reference counts for 'fd' and 'p', then
  // save them to 'fd_ref' and 'pipe_ref'.
  // Keep retrying until 'env->env_runs' is unchanged before and after
  // reading the reference counts.
  do {
    runs = env->env_runs;

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
 *
 * Hint:
 *   Use 'fd2data' to get the 'Pipe' referred by 'fd'.
 *   Use '_pipe_is_closed' to check if the pipe is closed.
 *   The parameter 'offset' isn't used here.
 */
static int pipe_read(struct Fd *fd, void *vbuf, u_int n, u_int offset) {
  struct Pipe *pipe = fd2data(fd);
  char *rbuf;

  // Use 'fd2data' to get the 'Pipe' referred by 'fd'.
  // Write a loop that transfers one byte in each iteration.
  // Check if the pipe is closed by '_pipe_is_closed'.
  // When the pipe buffer is empty:
  //  - If at least 1 byte is read, or the pipe is closed, just return the number
  //    of bytes read so far.
  //  - Otherwise, keep yielding until the buffer isn't empty or the pipe is closed.
  
  rbuf = (char *)vbuf;
  for (int i = 0; i < n; i++) {
    while (pipe->p_rpos >= pipe->p_wpos) {
      if (i > 0 || _pipe_is_closed(fd, pipe)) {
        return i;
      } else {
        syscall_yield();
      }
    }
    rbuf[i] = pipe->p_buf[pipe->p_rpos % PAGE_SIZE];
    pipe->p_rpos++;
  }

  user_panic("pipe_read not implemented");
}

/* Overview:
 *   Write 'n' bytes from 'vbuf' to the pipe referred by 'fd'.
 *
 * Post-Condition:
 *   Return the number of bytes written into the pipe.
 *
 * Hint:
 *   Use 'fd2data' to get the 'Pipe' referred by 'fd'.
 *   Use '_pipe_is_closed' to judge if the pipe is closed.
 *   The parameter 'offset' isn't used here.
 */
static int pipe_write(struct Fd *fd, const void *vbuf, u_int n, u_int offset) {
  int i;
  struct Pipe *p;
  char *wbuf;

  // Use 'fd2data' to get the 'Pipe' referred by 'fd'.
  // Write a loop that transfers one byte in each iteration.
  // If the bytes of the pipe used equals to 'PIPE_SIZE', the pipe is regarded as full.
  // Check if the pipe is closed by '_pipe_is_closed'.
  // When the pipe buffer is full:
  //  - If the pipe is closed, just return the number of bytes written so far.
  //  - If the pipe isn't closed, keep yielding until the buffer isn't full or the
  //    pipe is closed.
  /* Exercise 6.1: Your code here. (3/3) */
  p = fd2data(fd);
  wbuf = (char *)vbuf;
  for (i = 0; i < n; i++) {
    while (p->p_wpos - p->p_rpos >= PAGE_SIZE) {
      if (_pipe_is_closed(fd, p)) {
        return i;
      } else {
        syscall_yield();
      }
    }
    p->p_buf[p->p_wpos % PAGE_SIZE] = wbuf[i];
    p->p_wpos++;
  }

  user_panic("pipe_write not implemented");

  return n;
}

/* Overview:
 *   Check if the pipe referred by 'fdnum' is closed.
 *
 * Post-Condition:
 *   Return 1 if the pipe is closed.
 *   Return 0 if the pipe isn't closed.
 *   Return -E_INVAL if 'fdnum' is invalid or unmapped.
 *
 * Hint:
 *   Use '_pipe_is_closed'.
 */
int pipe_is_closed(int fdnum) {
  struct Fd *fd;
  struct Pipe *p;
  int r;

  // Step 1: Get the 'fd' referred by 'fdnum'.
  if ((r = fd_lookup(fdnum, &fd)) < 0) {
    return r;
  }
  // Step 2: Get the 'Pipe' referred by 'fd'.
  p = (struct Pipe *)fd2data(fd);
  // Step 3: Use '_pipe_is_closed' to judge if the pipe is closed.
  return _pipe_is_closed(fd, p);
}

/* Overview:
 *   Close the pipe referred by 'fd'.
 *
 * Post-Condition:
 *   Return 0 on success.
 *
 * Hint:
 *   Use 'syscall_mem_unmap' to unmap the pages.
 */
// 本质是解除文件描述符和管道数据的内存映射
static int pipe_close(struct Fd *fd) {
  syscall_mem_unmap(0, (void *)fd2data(fd));
  syscall_mem_unmap(0, fd);
  return 0;
}

static int pipe_stat(struct Fd *fd, struct Stat *stat) {
  return 0;
}
