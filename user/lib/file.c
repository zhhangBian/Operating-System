#include <fs.h>
#include <lib.h>

#define debug 0

// 为用户程序提供一系列库函数来完成文件的相关操作

static int file_close(struct Fd *fd);
static int file_read(struct Fd *fd, void *buf, u_int n, u_int offset);
static int file_write(struct Fd *fd, const void *buf, u_int n, u_int offset);
static int file_stat(struct Fd *fd, struct Stat *stat);

// Dot represents choosing the member within the struct declaration
// to initialize, with no need to consider the order of members.
struct Dev devfile = {
  .dev_id = 'f',
  .dev_name = "file",
  .dev_read = file_read,
  .dev_write = file_write,
  .dev_close = file_close,
  .dev_stat = file_stat,
};

// Overview:
//  Open a file (or directory).
//
// Returns:
//  the file descriptor on success,
//  the underlying error on failure.
// 按照mode模式打开path的文件，返回文件描述符的id
int open(const char *file_path, int mode) {
  // 获取一个新的文件描述符
  struct Fd *fd;
  // 获取新的文件描述符
  try(fd_alloc(&fd));

  // 使用文件服务IPC打开文件
  // 文件服务进程会通过fd设置相关信息
  try(fsipc_open(file_path, mode, fd));

  // 获取文件内容应该映射到的地址
  char *file_va = fd2data(fd);
  // 通过指针类型，改变文件描述符地址处信息的解释方式
  struct Filefd *filefd = (struct Filefd *)fd;
  // 这些信息之前由文件服务进程设置
  u_int file_size = filefd->f_file.f_size;
  u_int file_id = filefd->f_fileid;

  // 将文件的内容映加载到内存中，方式为建立相关的映射关系
  for (int i = 0; i < file_size; i += PTMAP) {
    try(fsipc_map(file_id, i, file_va + i));
  }

  // 返回文件描述符对应的id
  return fd2num(fd);
}

// Overview:
//  Close a file descriptor
// 关闭文件描述符对应的文件
int file_close(struct Fd *fd) {
  struct Filefd *file_fd = (struct Filefd *)fd;
  // 获取文件在内存中的虚拟地址
  void *file_va = fd2data(fd);
  // 获取文件对应的文件描述符id
  u_int file_id = file_fd->f_fileid;
  // 获取文件大小
  u_int file_size = file_fd->f_file.f_size;

  int func_info, i;

  // 将文件的每一页标记为脏
  for (i = 0; i < file_size; i += PTMAP) {
    if ((func_info = fsipc_dirty(file_id, i)) < 0) {
      debugf("cannot mark pages as dirty\n");
      return func_info;
    }
  }

  // 关闭文件
  if ((func_info = fsipc_close(file_id)) < 0) {
    debugf("cannot close the file\n");
    return func_info;
  }

  // 取消文件缓存在内存中的映射
  if (file_size == 0) {
    return 0;
  }
  for (i = 0; i < file_size; i += PTMAP) {
    if ((func_info = syscall_mem_unmap(0, (void *)(file_va + i))) < 0) {
      debugf("cannont unmap the file\n");
      return func_info;
    }
  }

  return 0;
}

// Overview:
//  Read 'n' bytes from 'fd' at the current seek position into 'buf'. Since files
//  are memory-mapped, this amounts to a memcpy() surrounded by a little red
//  tape to handle the file size and seek pointer.
// 从文件的offest处读取n个字节到buffer，返回实际读取的字节
static int file_read(struct Fd *fd, void *buffer, u_int n, u_int offset) {
  struct Filefd *file_fd = (struct Filefd *)fd;
  u_int file_size = file_fd->f_file.f_size;

  if (offset > file_size) {
    return 0;
  }
  // 多余则不读
  if (offset + n > file_size) {
    n = file_size - offset;
  }
  // 拷贝对应的地址
  memcpy(buffer, (char *)fd2data(fd) + offset, n);
  return n;
}

// Overview:
//  Find the virtual address of the page that maps the file block
//  starting at 'offset'.
// 获取fd_no对应文件的offest处的地址
int read_map(int fd_no, u_int offset, void **va_pointer) {
  struct Fd *fd;
  void *va;
  int func_info;

  // 获取对应的文件描述符
  if ((func_info = fd_lookup(fd_no, &fd)) < 0) {
    return func_info;
  }
  // 检查设备是否正确
  if (fd->fd_dev_id != devfile.dev_id) {
    return -E_INVAL;
  }

  if (offset >= MAXFILESIZE) {
    return -E_NO_DISK;
  }

  // 获取地址
  va = fd2data(fd) + offset;

  // 检查文件是否被载入磁盘：检查页表
  if (!(vpd[PDX(va)] & PTE_V) || !(vpt[VPN(va)] & PTE_V)) {
    return -E_NO_DISK;
  }

  // 地址写入指针
  *va_pointer = va;
  return 0;
}

// Overview:
//  Write 'n' bytes from 'buf' to 'fd' at the current seek position.
// 将buffer中n个字节写入到文件的offest处
static int file_write(struct Fd *fd, const void *buffer, u_int n, u_int offset) {
  struct Filefd *file_fd = (struct Filefd *)fd;
  u_int final_place = offset + n;
  int func_info;

  // 最后终点比最大文件尺寸，报错
  if (final_place > MAXFILESIZE) {
    return -E_NO_DISK;
  }

  // 比文件大，则扩容文件
  if (final_place > file_fd->f_file.f_size) {
    if ((func_info = ftruncate(fd2num(fd), final_place)) < 0) {
      return func_info;
    }
  }

  // 拷贝数据
  memcpy((char *)fd2data(fd) + offset, buffer, n);
  return n;
}

static int file_stat(struct Fd *fd, struct Stat *st) {
  struct Filefd *file_fd = (struct Filefd *)fd;

  strcpy(st->st_name, file_fd->f_file.f_name);
  st->st_size = file_fd->f_file.f_size;
  st->st_isdir = (file_fd->f_file.f_type == FTYPE_DIR);
	st->st_mode = (file_fd->f_file.f_mode) << 6;

  return 0;
}

// Overview:
//  Truncate or extend an open file to 'size' bytes
// 将文件的大小设置为size
int ftruncate(int fd_no, u_int new_size) {
  struct Fd *fd;
  int func_info, i;

  if (new_size > MAXFILESIZE) {
    return -E_NO_DISK;
  }

  // 获得第fd_no个文件描述符
  if ((func_info = fd_lookup(fd_no, &fd)) < 0) {
    return func_info;
  }
  // 判断设备是否相同
  if (fd->fd_dev_id != devfile.dev_id) {
    return -E_INVAL;
  }

  struct Filefd *file_fd = (struct Filefd *)fd;
  u_int old_size = file_fd->f_file.f_size;
  u_int file_id = file_fd->f_fileid;
  // 设置大小
  file_fd->f_file.f_size = new_size;
  if ((func_info = fsipc_set_size(file_id, new_size)) < 0) {
    return func_info;
  }

  // 获得文件在内存中的地址
  void *file_va = fd2data(fd);

  // 如果大小变大，则载入新页面
  for (i = ROUND(old_size, PTMAP); i < ROUND(new_size, PTMAP); i += PTMAP) {
    // 将磁盘块加载进内存
    if ((func_info = fsipc_map(file_id, i, file_va + i)) < 0) {
      int func_info_2 = fsipc_set_size(file_id, old_size);
      if (func_info_2 < 0) {
        return func_info_2;
      }
      return func_info;
    }
  }

  // 如果大小变小，则取消映射
  for (i = ROUND(new_size, PTMAP); i < ROUND(old_size, PTMAP); i += PTMAP) {
    if ((func_info = syscall_mem_unmap(0, file_va + i)) < 0) {
      user_panic("ftruncate: syscall_mem_unmap %08x: %d\n", file_va + i, func_info);
    }
  }

  return 0;
}

// Overview:
//  Delete a file or directory.
// 按路径移除文件
int remove(const char *path) {
  return fsipc_remove(path);
}

// Overview:
//  Synchronize disk with buffer cache
// 将文件系统的文件写回磁盘
int sync(void) {
  return fsipc_sync();
}

int chmod(const char *path, u_int mode, int type) {
	return fsipc_chmod(path, mode, type);
}
