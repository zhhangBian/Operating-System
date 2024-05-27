#ifndef _FSREQ_H_
#define _FSREQ_H_

#include <fs.h>
#include <types.h>

// Definitions for requests from clients to file system

// 文件操作的类型
enum {
  // 打开文件
	FSREQ_OPEN,
  // 建立磁盘块映射
	FSREQ_MAP,
	FSREQ_SET_SIZE,
  // 关闭文件
	FSREQ_CLOSE,
	FSREQ_DIRTY,
  // 删除文件
	FSREQ_REMOVE,
  // 同步文件，向磁盘写回被修改过的文件
	FSREQ_SYNC,
	MAX_FSREQNO,
};

// open操作的文件ipc请求
struct Fsreq_open {
	char req_path[MAXPATHLEN];
	u_int req_omode;
};

// map操作的文件ipc请求
struct Fsreq_map {
	int req_fileid;
	u_int req_offset;
};

// set_size操作的文件ipc请求
struct Fsreq_set_size {
	int req_fileid;
	u_int req_size;
};

// close操作的文件ipc请求
struct Fsreq_close {
	int req_fileid;
};

// dirty操作的文件ipc请求
struct Fsreq_dirty {
	int req_fileid;
	u_int req_offset;
};

// remove操作的文件ipc请求
struct Fsreq_remove {
	char req_path[MAXPATHLEN];
};

#endif
