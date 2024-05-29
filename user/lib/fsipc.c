#include <env.h>
#include <fsreq.h>
#include <lib.h>

#define debug 0

// fsipc.c：实现与文件系统服务进程的交互
// 文件系统的大部分操作并不在内核态中完成，而是交由一个文件系统服务进程处理

// 具体的实现见 fs/serv.c

// 缓冲区：数组实际上是申请了空间
// 缓冲区大小为一个页面，还进行了对齐
u_char fsipcbuf[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

// Overview:
//  Send an IPC request to the file server, and wait for a reply.
//
// Parameters:
//  @type: request code, passed as the simple integer IPC value.
//  @fsreq: page to send containing additional request data, usually fsipcbuf.
//          Can be modified by server to return additional response info.
//  @dstva: virtual address at which to receive reply page, 0 if none.
//  @*perm: permissions of received page.
//
// Returns:
//  0 if successful,
//  < 0 on failure.
// 文件服务IPC，向文件服务进程发送信息，并接受返回信息
static int fsipc(u_int type, void *request, void *dst_va, u_int *permission) {
  // 强制向第二个进程发送，即文件服务进程，必须使其为第二个进程
  ipc_send(envs[1].env_id, // 接受进程的envid：必须为文件服务进程
           type,      // 发送的值：文件操作的类型
           request,   // 共享的数据虚拟地址，设置为请求类型
           PTE_D);    // 共享区域的权限
  // 等待从文件服务接受信息
  // 接受到的页面的权限由文件服务进程设置
  // 实际上不关注发送id：肯定是文件服务进程，一定是第二个进程
  u_int send_id;
  return ipc_recv(&send_id, dst_va, permission);
}

// Overview:
//  Send file-open request to the file server. Includes path and
//  omode in request, sets *fileid and *size from reply.
//
// Returns:
//  0 on success,
//  < 0 on failure.
// 利用IPC向文件处理服务发送打开文件请求
int fsipc_open(const char *path, u_int mode, struct Fd *fd) {
  // 文件服务进程贡献页面的权限，这里实际上没有用到
  u_int permission;

  // 将fsipcbuf空间用作请求区，向其中写入了请求打开的文件路径req_path和打开方式req_omode
  struct Fsreq_open *request = (struct Fsreq_open *)fsipcbuf;

  // 路径过长，报错
  if (strlen(path) >= MAXPATHLEN) {
    return -E_BAD_PATH;
  }
  // 写入文件路径
  strcpy((char *)request->req_path, path);
  // 写入文件打开方式
  request->req_omode = mode;
  // 进行文件ipc
  return fsipc(FSREQ_OPEN, request, fd, &permission);
}

// Overview:
//  Make a map-block request to the file server. We send the fileid and
//  the (byte) offset of the desired block in the file, and the server sends
//  us back a mapping for a page containing that block.
//
// Returns:
//  0 on success,
//  < 0 on failure.
// 将磁盘块加载进内存，建立一个映射
int fsipc_map(u_int file_id, u_int offset, void *dst_va) {
  int func_info;
  u_int permission;
  // 建立磁盘块映射请求
  struct Fsreq_map *request = (struct Fsreq_map *)fsipcbuf;
  request->req_fileid = file_id;
  request->req_offset = offset;

  // 向文件服务进程发送简历映射请求、
  if ((func_info = fsipc(FSREQ_MAP, request, dst_va, &permission)) < 0) {
    return func_info;
  }
  // 检查共享页面的权限
  if ((permission & ~(PTE_D | PTE_LIBRARY)) != (PTE_V)) {
    user_panic("fsipc_map: unexpected permissions %08x for dstva %08x", permission, dst_va);
  }

  return 0;
}

// Overview:
//  Make a set-file-size request to the file server.
// 发送一个set_size请求
int fsipc_set_size(u_int file_id, u_int size) {
  struct Fsreq_set_size *requset = (struct Fsreq_set_size *)fsipcbuf;
  requset->req_fileid = file_id;
  requset->req_size = size;

  return fsipc(FSREQ_SET_SIZE, requset, 0, 0);
}

// Overview:
//  Make a file-close request to the file server. After this the fileid is invalid.
// 发送一个关闭文件请求
int fsipc_close(u_int file_id) {
  struct Fsreq_close *request = (struct Fsreq_close *)fsipcbuf;
  request->req_fileid = file_id;

  return fsipc(FSREQ_CLOSE, request, 0, 0);
}

// Overview:
//  Ask the file server to mark a particular file block dirty.
// 将文件块offest处的磁盘块标记为脏
int fsipc_dirty(u_int file_id, u_int offset) {
  struct Fsreq_dirty *request = (struct Fsreq_dirty *)fsipcbuf;
  request->req_fileid = file_id;
  request->req_offset = offset;

  return fsipc(FSREQ_DIRTY, request, 0, 0);
}

// Overview:
//  Ask the file server to delete a file, given its path.
// 删除文件，通过给出路径实现
int fsipc_remove(const char *path) {
  // 检查路径的长度
  int len = strlen(path);
  if (len == 0 || len > MAXPATHLEN) {
    return -E_BAD_PATH;
  }

  // 设置文件删除请求
  struct Fsreq_remove *request = (struct Fsreq_remove *)fsipcbuf;
  // 复制需要删除的文件路径
  strcpy(request->req_path, path);

  // 向文件服务进程发送删除请求
  fsipc(FSREQ_REMOVE, request, 0, 0);
}

// Overview:
//  Ask the file server to update the disk by writing any dirty
//  blocks in the buffer cache.
// 将文件系统的内容写回磁盘
int fsipc_sync(void) {
  return fsipc(FSREQ_SYNC, fsipcbuf, 0, 0);
}
