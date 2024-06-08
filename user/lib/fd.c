#include <env.h>
#include <fd.h>
#include <lib.h>
#include <mmu.h>

// 保存了相应的设备
static struct Dev *devtab[] = {
  &devfile,
  &devcons,
#if !defined(LAB) || LAB >= 6
  &devpipe,
#endif
  0
};

// 找到dev_id对应的设备，保存到指针
int dev_lookup(int dev_id, struct Dev **dev) {
  // 遍历设备列表，寻找是否有对应id的设备
  for (int i = 0; devtab[i]; i++) {
    if (devtab[i]->dev_id == dev_id) {
      *dev = devtab[i];
      return 0;
    }
  }
  // 找不到设备
  *dev = NULL;
  debugf("[%08x] unknown device type %d\n", env->env_id, dev_id);
  return -E_INVAL;
}

// Overview:
//  Find the smallest i from 0 to MAXFD-1 that doesn't have its fd page mapped.
//
// Post-Condition:
//   Set *fd to the fd page virtual address.
//   (Do not allocate any pages: It is up to the caller to allocate
//    the page, meaning that if someone calls fd_alloc twice
//    in a row without allocating the first page we returned, we'll
//    return the same page at the second time.)
//   Return 0 on success, or an error code on error.
// 获取当前可使用的、id最小的文件描述符
// 为什么不返回fd形式的数据结构，而是使用地址进行管理：
// 文件描述符并不是在用户进程中被创建的，而是在文件系统服务进程中创建，被共享到用户进程的地址区域中的
int fd_alloc(struct Fd **fd) {
  u_int fd_va;
  u_int fd_no;
  // 寻找id最小的可用的文件描述符
  for (fd_no = 0; fd_no < MAXFD - 1; fd_no++) {
    // 获取fd_no对应的文件描述符地址
    // fd的地址是固定的，主要对地址进行操作，基本没对数据结构进行操作
    fd_va = INDEX2FD(fd_no);

    // 没有其他数据结构来维护文件描述符是否被使用，而是直接查页表
    // 文件描述符是在文件系统服务进程中创建，被共享到用户进程的地址区域中的

    // 检查页目录项：用除法直接获取对应页目录项号
    int pde_no = fd_va / PDMAP;
    // 得到相应的页目录，页目录无效，则后续的页表肯定无效，直接返回va
    if ((vpd[pde_no] & PTE_V) == 0) {
      *fd = (struct Fd *)fd_va;
      return 0;
    }

    // 检查页表项：用除法直接获取对应页表项号
    int pte_no = fd_va / PTMAP;
    // 得到相应的页表，页表无效，通过指针返回va
    if ((vpt[pte_no] & PTE_V) == 0) {
      *fd = (struct Fd *)fd_va;
      return 0;
    }
  }

  return -E_MAX_OPEN;
}

// 接触文件描述符的占用：直接取消其地址映射
void fd_close(struct Fd *fd) {
  panic_on(syscall_mem_unmap(0, fd));
}

// Overview:
//  Find the 'Fd' page for the given fd number.
//
// Post-Condition:
//  Return 0 and set *fd to the pointer to the 'Fd' page on success.
//  Return -E_INVAL if 'fdnum' is invalid or unmapped.
// 找到fd_no对应的fd，同时检查fd是否正在被使用，不再使用则报错
int fd_lookup(int fd_no, struct Fd **fd) {
  // 判断fd是否合法
  if (fd_no >= MAXFD) {
    return -E_INVAL;
  }

  u_int fd_va = INDEX2FD(fd_no);
  // 判断fd是否在被使用：通过页表判断
  int pte_no = fd_va / PTMAP;
  if ((vpt[pte_no] & PTE_V) != 0) {
    *fd = (struct Fd *)fd_va;
    return 0;
  }

  return -E_INVAL;
}

// 获取文件描述符对应的，文件应该被映射到的虚拟地址
void *fd2data(struct Fd *fd) {
  return (void *)INDEX2DATA(fd2num(fd));
}

// 获取文件描述符的id
int fd2num(struct Fd *fd) {
  return ((u_int)fd - FDTABLE) / PTMAP;
}

// 获取id对应的文件描述符
int num2fd(int fd) {
  return fd * PTMAP + FDTABLE;
}

// 关闭文件，会同时关闭文件描述符的占用
int close(int fd_no) {
  struct Dev *dev;
  struct Fd *fd;
  int func_info;

  // 获得fd
  if((func_info = fd_lookup(fd_no, &fd)) < 0) {
    return func_info;
  }
  // 获得对应设备
  if((func_info = dev_lookup(fd->fd_dev_id, &dev)) < 0) {
    return func_info;
  }

  func_info = (*dev->dev_close)(fd);
  // 关闭了文件，需要同时接触占用文件描述符
  fd_close(fd);

  return func_info;
}

// 关闭所有文件
void close_all(void) {
  // 遍历打开文件列表
  for (int i = 0; i < MAXFD; i++) {
    close(i);
  }
}

/* Overview:
 *   Duplicate the file descriptor.
 *
 * Post-Condition:
 *   Return 'newfdnum' on success.
 *   Return the corresponding error code on error.
 */
// 复制打开文件的内容到新的文件描述符id
int dup(int old_fd_no, int new_fd_no) {
  void *old_file_va, *new_file_va;
  struct Fd *old_fd, *new_fd;
  u_int pte;
  int func_info;
  int i;

  // 检查旧的文件描述符是否有效，同时获取旧文件描述符
  if ((func_info = fd_lookup(old_fd_no, &old_fd)) < 0) {
    return func_info;
  }

  // 无论新文件描述符id是否有效，都强制关闭
  close(new_fd_no);
  // 获取新文件描述符
  // 由于上一步强制关闭了，故不能使用fd_lookup
  new_fd = (struct Fd *)INDEX2FD(new_fd_no);

  // 复制打开文件的内容
  old_file_va = fd2data(old_fd);
  new_file_va = fd2data(new_fd);
  // 通过建立映射关系复制文件描述符的内容
  if ((func_info = syscall_mem_map(0, old_fd, 0, new_fd, vpt[VPN(old_fd)] & (PTE_D | PTE_LIBRARY))) < 0) {
    goto err;
  }

  // 如果页目录有效
  if (vpd[PDX(old_file_va)]) {
    // 遍历对应的页表项
    for (i = 0; i < PDMAP; i += PTMAP) {
      // 获取页表
      pte = vpt[VPN(old_file_va + i)];
      // 如果页表有效
      if (pte & PTE_V) {
        // 复制页表内容建立映射
        if ((func_info = syscall_mem_map(
              0, (void *)(old_file_va + i),
              0, (void *)(new_file_va + i),
              pte & (PTE_D | PTE_LIBRARY)
          )) < 0) {
          goto err;
        }
      }
    }
  }

  return new_fd_no;

// 异常处理程序
err:
  // 取消所有地址映射
  panic_on(syscall_mem_unmap(0, new_fd));

  for (i = 0; i < PDMAP; i += PTMAP) {
    panic_on(syscall_mem_unmap(0, (void *)(new_file_va + i)));
  }

  return func_info;
}

// Overview:
//  Read at most 'n' bytes from 'fd' at the current seek position into 'buffer'.
//
// Post-Condition:
//  Update seek position.
//  Return the number of bytes read successfully.
//  Return < 0 on error.
// 从当前文件的读写指针继续读取n个字节到bufferfer中
int read(int fd_no, void *buffer, u_int n) {
  struct Dev *dev;
  struct Fd *fd;
  int func_info;

  // 获得fd
  if((func_info = fd_lookup(fd_no, &fd)) < 0) {
    return func_info;
  }
  // 获得对应设备
  if((func_info = dev_lookup(fd->fd_dev_id, &dev)) < 0) {
    return func_info;
  }

  // 检查文件读写模式是否符合
  if ((fd->fd_omode & O_ACCMODE) == O_WRONLY) {
    return -E_INVAL;
  }

  // 从文件的读写指针继续读n个字节到buffer中
  func_info = dev->dev_read(fd, buffer, n, fd->fd_offset);

  // 更新文件的读写指针
  /* Hint: DO NOT add a null terminator to the end of the buffer!
   *  A character bufferfer is not a C string.
   *  Only the memory within [buffer, buffer+n) is safe to use.
   */
  if (func_info > 0) {
    fd->fd_offset += func_info;
  }

  return func_info;
}

// 从文件中读取n个字节，但做了溢出检查
int readn(int fd_no, void *buffer, u_int n) {
  // 当前读取的字节数
  int offest;
  int now_read;

  for (offest = 0; offest < n; offest += now_read) {
    now_read = read(fd_no, (char *)buffer + offest, n - offest);

    // 出错，直接返回
    if (now_read < 0) {
      return now_read;
    }
    // 溢出检查，不再读取
    if (now_read == 0) {
      break;
    }
  }
  // 返回读取的字节数
  return offest;
}

// 对文件的写
int write(int fd_no, const void *buffer, u_int n) {
  int func_info;
  struct Dev *dev;
  struct Fd *fd;

  // 寻找文件描述符
  if((func_info = fd_lookup(fd_no, &fd)) < 0) {
    return func_info;
  }
  // 寻找文件所在的设备
  if((func_info = dev_lookup(fd->fd_dev_id, &dev)) < 0) {
    return func_info;
  }

  // 检查文件读写模式是否符合
  if ((fd->fd_omode & O_ACCMODE) == O_RDONLY) {
    return -E_INVAL;
  }

  // 返回文件读写的偏移量
  func_info = dev->dev_write(fd, buffer, n, fd->fd_offset);
  if (func_info > 0) {
    fd->fd_offset += func_info;
  }

  return func_info;
}

// 找到fd_no文件对应offest处
int seek(int fd_no, u_int offset) {
  struct Fd *fd;
  int func_info;

  if ((func_info = fd_lookup(fd_no, &fd)) < 0) {
    return func_info;
  }

  fd->fd_offset = offset;

  return 0;
}

int fstat(int fdnum, struct Stat *stat) {
  int r;
  struct Dev *dev = NULL;
  struct Fd *fd;

  if ((r = fd_lookup(fdnum, &fd)) < 0 || (r = dev_lookup(fd->fd_dev_id, &dev)) < 0) {
    return r;
  }

  stat->st_name[0] = 0;
  stat->st_size = 0;
  stat->st_isdir = 0;
  stat->st_dev = dev;
  return (*dev->dev_stat)(fd, stat);
}

int stat(const char *path, struct Stat *stat) {
  int fd, r;

  if ((fd = open(path, O_RDONLY)) < 0) {
    return fd;
  }

  r = fstat(fd, stat);
  close(fd);
  return r;
}
