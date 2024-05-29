/*
 * File system server main loop -
 * serves IPC requests from other environments.
 */

// 文件服务进程的主函数所在地

#include "serv.h"
#include <fd.h>
#include <fsreq.h>
#include <lib.h>
#include <mmu.h>

/*
 * Fields
 * o_file: mapped descriptor for open file
 * o_fileid: file id
 * o_mode: open mode
 * o_ff: va of filefd page
 */
// 记录打开文件的信息
// 用户界面用fd，文件服务进程用Open
// Open和fd是一一对应的，可以通过o_ff访问
struct Open {
  // 指向打开文件的指针
  struct File *o_file;
  // open块自身的id
  u_int o_fileid;
  // 文件的打开方式
  int o_mode;
  // 文件描述符的地址，通过地址访问
  struct Filefd *o_ff;
};

/*
 * Max number of open files in the file system at once
 */
// 整个操作系统最大同时打开的文件数目
#define MAXOPEN 1024

#define FILEVA 0x60000000

/*
 * Open file table, a per-environment array of open files
 */
// 用于记录整个操作系统中所有处于打开状态的文件
// 用户界面用fd，文件服务进程用Open
struct Open opentab[MAXOPEN];

/*
 * Virtual address at which to receive page mappings containing client requests.
 */
// 用于接收文件服务发送/需求进程  共享内容的地址
#define REQVA 0x0ffff000

/*
 * Overview:
 *  Set up open file table and connect it with the file cache.
 */
// 文件服务进程初始化
void serve_init(void) {
  int i;
  // Set virtual address to map.
  u_int virtual_address = FILEVA;

  // Initial array opentab.
  // 初始化打开文件记录表
  // 设置opentab的地址，存储在FILEVA，和Filefd 是一一对应的关系
  for (i = 0; i < MAXOPEN; i++) {
    opentab[i].o_fileid = i;
    opentab[i].o_ff = (struct Filefd *)virtual_address;
    virtual_address += BLOCK_SIZE;
  }
}

/*
 * Overview:
 *  Allocate an open file.
 * Parameters:
 *  o: the pointer to the allocated open descriptor.
 * Return:
 * 0 on success, - E_MAX_OPEN on error
 */
// 获取一个新的打开文件描述
int open_alloc(struct Open **open_pointer) {
  int func_info;

  // Find an available open-file table entry
  for (int i = 0; i < MAXOPEN; i++) {
    // 查看Filefd地址对应的页表项是否有效
    // opentab作为数组，其中元素已经分配了物理内存，故通过指针查看别的地方
    switch (pageref(opentab[i].o_ff)) {
      // 无效则分配内存
      case 0:
        if ((func_info = syscall_mem_alloc(0, opentab[i].o_ff, PTE_D | PTE_LIBRARY)) < 0) {
          return func_info;
        }
        // 不break，继续到下面函数进行初始化
      // 只打开一次，曾经被使用过，但现在不被任何用户进程使用的文件，清零后复用
      case 1:
        // 通过指针返回
        *open_pointer = &opentab[i];
        // 初始化：清零
        memset((void *)opentab[i].o_ff, 0, BLOCK_SIZE);
        // 返回open块的id
        return (*open_pointer)->o_fileid;
        break;
      // 其余情况继续查找
      default:
        continue;
    }
  }
  // 没有可用的，报错
  return -E_MAX_OPEN;
}

// Overview:
//  Look up an open file for envid.
/*
 * Overview:
 *  Look up an open file by using envid and fileid. If found,
 *  the `po` pointer will be pointed to the open file.
 * Parameters:
 *  envid: the id of the request process.
 *  fileid: the id of the file.
 *  po: the pointer to the open file.
 * Return:
 * 0 on success, -E_INVAL on error (fileid illegal or file not open by envid)
 *
 */
// 获取对应id的open块
int open_lookup(u_int envid, u_int open_id, struct Open **open_pointer) {
  struct Open *open;
  // 检查id是否有效
  if (open_id >= MAXOPEN) {
    return -E_INVAL;
  }

  // 获取open块
  open = &opentab[open_id];
  // 判断是否有效
  if (pageref(open->o_ff) <= 1) {
    return -E_INVAL;
  }

  *open_pointer = open;
  return 0;
}

/*
 * Functions with the prefix "serve_" are those who
 * conduct the file system requests from clients.
 * The file system receives the requests by function
 * `ipc_recv`, when the requests are received, the
 * file system will call the corresponding `serve_`
 * and return the result to the caller by function
 * `ipc_send`.
 */

/*
 * Overview:
 * Serve to open a file specified by the path in `rq`.
 * It will try to alloc an open descriptor, open the file
 * and then save the info in the File descriptor. If everything
 * is done, it will use the ipc_send to return the FileFd page
 * to the caller.
 * Parameters:
 * envid: the id of the request process.
 * rq: the request, which contains the path and the open mode.
 * Return:
 * if Success, return the FileFd page to the caller by ipc_send,
 * Otherwise, use ipc_send to return the error value to the caller.
 */
// 文件服务进程的打开文件操作
// 如果出现异常，终止函数，使用ipc_send发送回信息
void serve_open(u_int envid, struct Fsreq_open *request) {
  struct File *file;
  struct Open *open;
  int func_info;

  // 申请一个存储文件打开信息的open控制块
  if ((func_info = open_alloc(&open)) < 0) {
    ipc_send(envid, func_info, 0, 0);
    return;
  }


  // 如果文件请求是 不存在则创建 访问文件模式
  if (request->req_omode & O_CREAT) {
    // 创建文件
    func_info = file_create(request->req_path, &file);
    // 如果发生异常  且不是  文件已存在异常
    if(func_info < 0 && func_info != -E_FILE_EXISTS) {
      ipc_send(envid, func_info, 0, 0);
      return;
    }
  }

  // 使用file_open打开文件
  if ((func_info = file_open(request->req_path, &file)) < 0) {
    ipc_send(envid, func_info, 0, 0);
    return;
  }

	if(request->req_omode & file->f_mode == 0) {
		ipc_send(envid, -E_PERM_DENY, 0, 0);
		return;
	}

  // 如果是  缩减到0长度  模式
  if (request->req_omode & O_TRUNC) {
    if ((func_info = file_set_size(file, 0)) < 0) {
      ipc_send(envid, func_info, 0, 0);
    }
  }

  // 记录打开信息
  open->o_file = file;
  open->o_mode = request->req_omode;
  // 填写Filefd内容
  struct Filefd *file_fd = open->o_ff;
  file_fd->f_file = *file;
  file_fd->f_fileid = open->o_fileid;
  file_fd->f_fd.fd_omode = request->req_omode;
  // 设置文件描述符对应的设备为devfile
  file_fd->f_fd.fd_dev_id = devfile.dev_id;

  ipc_send(envid, 0, file_fd, PTE_D | PTE_LIBRARY);
}

/*
 * Overview:
 *  Serve to map the file specified by the fileid in `rq`.
 *  It will use the fileid and envid to find the open file and
 *  then call the `file_get_block` to get the block and use
 *  the `ipc_send` to return the block to the caller.
 * Parameters:
 *  envid: the id of the request process.
 *  rq: the request, which contains the fileid and the offset.
 * Return:
 *  if Success, use ipc_send to return zero and  the block to
 *  the caller.Otherwise, return the error value to the caller.
 */
// 将磁盘块载入内存
void serve_map(u_int envid, struct Fsreq_map *request) {
  struct Open *open;
  int func_info;

  // 获得对应的open块
  if ((func_info = open_lookup(envid, request->req_fileid, &open)) < 0) {
    ipc_send(envid, func_info, 0, 0);
    return;
  }

  // 获得磁盘块在文件中的编号f_no
  u_int file_block_no = request->req_offset / BLOCK_SIZE;
  // 获得磁盘块在磁盘中的编号b_no
  void *block_no_pointer;
  if ((func_info = file_get_block(open->o_file, file_block_no, &block_no_pointer)) < 0) {
    ipc_send(envid, func_info, 0, 0);
    return;
  }

  ipc_send(envid, 0, block_no_pointer, PTE_D | PTE_LIBRARY);
}

/*
 * Overview:
 *  Serve to set the size of a file specified by the fileid in `rq`.
 *  It tries to find the open file by using open_lookup function and then
 *  call the `file_set_size` to set the size of the file.
 * Parameters:
 *  envid: the id of the request process.
 *  rq: the request, which contains the fileid and the size.
 * Return:
 * if Success, use ipc_send to return 0 to the caller. Otherwise,
 * return the error value to the caller.
 */
// 设置文件的尺寸
void serve_set_size(u_int envid, struct Fsreq_set_size *request) {
  struct Open *open;
  int func_info;

  // 获取open块
  if ((func_info = open_lookup(envid, request->req_fileid, &open)) < 0) {
    ipc_send(envid, func_info, 0, 0);
    return;
  }

  // 设置文件的尺寸
  if ((func_info = file_set_size(open->o_file, request->req_size)) < 0) {
    ipc_send(envid, func_info, 0, 0);
    return;
  }

  ipc_send(envid, 0, 0, 0);
}

/*
 * Overview:
 *  Serve to close a file specified by the fileid in `rq`.
 *  It will use the fileid and envid to find the open file and
 * 	then call the `file_close` to close the file.
 * Parameters:
 *  envid: the id of the request process.
 * 	rq: the request, which contains the fileid.
 * Return:
 *  if Success, use ipc_send to return 0 to the caller.Otherwise,
 *  return the error value to the caller.
 */
// 关闭文件
void serve_close(u_int envid, struct Fsreq_close *request) {
  struct Open *open;
  int func_info;

  if ((func_info = open_lookup(envid, request->req_fileid, &open)) < 0) {
    ipc_send(envid, func_info, 0, 0);
    return;
  }
  // 关闭文件
  file_close(open->o_file);
  ipc_send(envid, 0, 0, 0);
}

/*
 * Overview:
 *  Serve to remove a file specified by the path in `req`.
 *  It calls the `file_remove` to remove the file and then use
 *  the `ipc_send` to return the result to the caller.
 * Parameters:
 *  envid: the id of the request process.
 *  rq: the request, which contains the path.
 * Return:
 *  the result of the file_remove to the caller by ipc_send.
 */
// 移除path处的文件，返回值为移除函数的返回值
void serve_remove(u_int envid, struct Fsreq_remove *request) {
  int func_info = file_remove(request->req_path);
  ipc_send(envid, func_info, 0, 0);
}

/*
 * Overview:
 *  Serve to dirty the file.
 *  It will use the fileid and envid to find the open file and
 * 	then call the `file_dirty` to dirty the file.
 * Parameters:
 *  envid: the id of the request process.
 *  rq: the request, which contains the fileid and the offset.
 * `Return`:
 *  if Success, use ipc_send to return 0 to the caller. Otherwise,
 *  return the error value to the caller.
 */
// 将文件控制块标记为脏
void serve_dirty(u_int envid, struct Fsreq_dirty *request) {
  struct Open *open;
  int func_info;

  // 获取文件id对应的open块
  if ((func_info = open_lookup(envid, request->req_fileid, &open)) < 0) {
    ipc_send(envid, func_info, 0, 0);
    return;
  }
  // 将文件控制块标记为脏
  if ((func_info = file_dirty(open->o_file, request->req_offset)) < 0) {
    ipc_send(envid, func_info, 0, 0);
    return;
  }

  ipc_send(envid, 0, 0, 0);
}

/*
 * Overview:
 *  Serve to sync the file system.
 *  it calls the `fs_sync` to sync the file system.
 *  and then use the `ipc_send` and `return` 0 to tell the caller
 *  file system is synced.
 */
// 将文件系统的文件更新回磁盘
void serve_sync(u_int envid) {
  fs_sync();
  ipc_send(envid, 0, 0, 0);
}

void serve_chmod(u_int envid, struct Fsreq_chmod *rq) {
	struct File* file;
	int r = file_open(rq->req_path, &file);
	if(r<0) {
		ipc_send(envid, r, 0, 0);
		return;
	}
	debugf("%d---\n",file->f_mode);

	int type = rq->req_type;
	uint32_t mode = rq->req_mode;
	debugf("%d %d %d %s\n", type,file->f_mode, mode, rq->req_path);

	if(type == 0) {
		file->f_mode = mode;
	}
	else if(type == 1) {
		file->f_mode = file->f_mode | mode;
	}
	else if(type == 2) {
		file->f_mode = file->f_mode & (~mode);
	}

	debugf("%d %d\n",file->f_mode, ~mode);
	file_close(file);

	ipc_send(envid, 0, 0, 0);
}

/*
 * The serve function table
 * File system use this table and the request number to
 * call the corresponding serve function.
 */
// 文件服务需求函数
void *serve_table[MAX_FSREQNO] = {
  // 文件服务进程的打开文件操作
  [FSREQ_OPEN]      = serve_open,
  // 将磁盘块载入内存
  [FSREQ_MAP]       = serve_map,
  // 设置文件的尺寸
  [FSREQ_SET_SIZE]  = serve_set_size,
  // 关闭文件
  [FSREQ_CLOSE]     = serve_close,
  // 将文件控制块标记为脏
  [FSREQ_DIRTY]     = serve_dirty,
  // 移除path处的文件，返回值为移除函数的返回值
  [FSREQ_REMOVE]    = serve_remove,
  // 将文件系统的文件更新回磁盘
  [FSREQ_SYNC]      = serve_sync,
	[FSREQ_CHMOD] = serve_chmod,

};

/*
 * Overview:
 *  The main loop of the file system server.
 *  It receives requests from other processes, if no request,
 *  the kernel will schedule other processes. Otherwise, it will
 *  call the corresponding serve function with the reqeust number
 *  to handle the request.
 */
// 文件服务进程的主函数，接收其他进程发送的文件服务请求
void serve(void) {
  u_int request;
  u_int permission;
  u_int send_id;
  // 文件服务需要调用的函数
  void (*func)(u_int, u_int);

  // 通过循环保持持续响应
  for (;;) {
    permission = 0;
    request = ipc_recv(&send_id, (void *)REQVA, &permission);

    // All requests must contain an argument page
    // 所有需求必须共享权限为有效
    if (!(permission & PTE_V)) {
      debugf("Invalid request from %08x: no argument page\n", send_id);
      // just leave it hanging, waiting for the next request.
      // 等待下一个响应
      continue;
    }

    // 不合理的文件服务请求
    if (request < 0 || request >= MAX_FSREQNO) {
      debugf("Invalid request code %d from %08x\n", request, send_id);
      panic_on(syscall_mem_unmap(0, (void *)REQVA));
      continue;
    }

    // 调用需求响应函数
    func = serve_table[request];
    func(send_id, REQVA);

    // Unmap the argument page.
    panic_on(syscall_mem_unmap(0, (void *)REQVA));
  }
}

/*
 * Overview:
 *  The main function of the file system server.
 *  It will call the `serve_init` to initialize the file system
 *  and then call the `serve` to handle the requests.
 */
// 文件服务进程的主程序
int main() {
  user_assert(sizeof(struct File) == FILE_STRUCT_SIZE);

  debugf("FS is running\n");
  // 文件服务进程初始化
  serve_init();
  // 文件系统初始化
  fs_init();
  // 开始运行文件服务进程
  serve();

  return 0;
}
