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

  // Tell the file server the dirty page.
  for (i = 0; i < file_size; i += PTMAP) {
    if ((func_info = fsipc_dirty(file_id, i)) < 0) {
      debugf("cannot mark pages as dirty\n");
      return func_info;
    }
  }

  // Request the file server to close the file with fsipc.
  if ((func_info = fsipc_close(file_id)) < 0) {
    debugf("cannot close the file\n");
    return func_info;
  }

  // Unmap the content of file, release memory.
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
static int file_read(struct Fd *fd, void *buf, u_int n, u_int offset) {
  u_int size;
  struct Filefd *f;
  f = (struct Filefd *)fd;

  // Avoid reading past the end of file.
  size = f->f_file.f_size;

  if (offset > size) {
    return 0;
  }

  if (offset + n > size) {
    n = size - offset;
  }

  memcpy(buf, (char *)fd2data(fd) + offset, n);
  return n;
}

// Overview:
//  Find the virtual address of the page that maps the file block
//  starting at 'offset'.
int read_map(int fdnum, u_int offset, void **blk) {
  int r;
  void *va;
  struct Fd *fd;

  if ((r = fd_lookup(fdnum, &fd)) < 0) {
    return r;
  }

  if (fd->fd_dev_id != devfile.dev_id) {
    return -E_INVAL;
  }

  va = fd2data(fd) + offset;

  if (offset >= MAXFILESIZE) {
    return -E_NO_DISK;
  }

  if (!(vpd[PDX(va)] & PTE_V) || !(vpt[VPN(va)] & PTE_V)) {
    return -E_NO_DISK;
  }

  *blk = (void *)va;
  return 0;
}

// Overview:
//  Write 'n' bytes from 'buf' to 'fd' at the current seek position.
static int file_write(struct Fd *fd, const void *buf, u_int n, u_int offset) {
  int r;
  u_int tot;
  struct Filefd *f;

  f = (struct Filefd *)fd;

  // Don't write more than the maximum file size.
  tot = offset + n;

  if (tot > MAXFILESIZE) {
    return -E_NO_DISK;
  }
  // Increase the file's size if necessary
  if (tot > f->f_file.f_size) {
    if ((r = ftruncate(fd2num(fd), tot)) < 0) {
      return r;
    }
  }

  // Write the data
  memcpy((char *)fd2data(fd) + offset, buf, n);
  return n;
}

static int file_stat(struct Fd *fd, struct Stat *st) {
  struct Filefd *f;

  f = (struct Filefd *)fd;

  strcpy(st->st_name, f->f_file.f_name);
  st->st_size = f->f_file.f_size;
  st->st_isdir = f->f_file.f_type == FTYPE_DIR;
  return 0;
}

// Overview:
//  Truncate or extend an open file to 'size' bytes
int ftruncate(int fdnum, u_int size) {
  int i, r;
  struct Fd *fd;
  struct Filefd *f;
  u_int oldsize, fileid;

  if (size > MAXFILESIZE) {
    return -E_NO_DISK;
  }

  if ((r = fd_lookup(fdnum, &fd)) < 0) {
    return r;
  }

  if (fd->fd_dev_id != devfile.dev_id) {
    return -E_INVAL;
  }

  f = (struct Filefd *)fd;
  fileid = f->f_fileid;
  oldsize = f->f_file.f_size;
  f->f_file.f_size = size;

  if ((r = fsipc_set_size(fileid, size)) < 0) {
    return r;
  }

  void *va = fd2data(fd);

  // Map any new pages needed if extending the file
  for (i = ROUND(oldsize, PTMAP); i < ROUND(size, PTMAP); i += PTMAP) {
    if ((r = fsipc_map(fileid, i, va + i)) < 0) {
      int _r = fsipc_set_size(fileid, oldsize);
      if (_r < 0) {
        return _r;
      }
      return r;
    }
  }

  // Unmap pages if truncating the file
  for (i = ROUND(size, PTMAP); i < ROUND(oldsize, PTMAP); i += PTMAP) {
    if ((r = syscall_mem_unmap(0, (void *)(va + i))) < 0) {
      user_panic("ftruncate: syscall_mem_unmap %08x: %d\n", va + i, r);
    }
  }

  return 0;
}

// Overview:
//  Delete a file or directory.
int remove(const char *path) {
  // Call fsipc_remove.

  /* Exercise 5.13: Your code here. */
  return fsipc_remove(path);
}

// Overview:
//  Synchronize disk with buffer cache
int sync(void) {
  return fsipc_sync();
}
