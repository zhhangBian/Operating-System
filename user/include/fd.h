#ifndef _USER_FD_H_
#define _USER_FD_H_ 1

#include <fs.h>

#define debug 0
// 文件描述符的最大个数
#define MAXFD 32
#define FILEBASE 0x60000000
// 文件描述符的起始地址
#define FDTABLE (FILEBASE - PDMAP)

// 获取相应的文件描述符
#define INDEX2FD(i) (FDTABLE + (i)*PTMAP)
#define INDEX2DATA(i) (FILEBASE + (i)*PDMAP)

// pre-declare for forward references
struct Fd;
struct Stat;
struct Dev;

// Device struct:
// It is used to read and write data from corresponding device.
// We can use the five functions to handle data.
// There are three devices in this OS: file, console and pipe.
// 设备类型，设置了相应的设备id和对设备的基本操作，通过函数指针实现
// MOS中共有文件、终端、管道三种文件类型
struct Dev {
	int dev_id;
	char *dev_name;
	int (*dev_read)(struct Fd *, void *, u_int, u_int);
	int (*dev_write)(struct Fd *, const void *, u_int, u_int);
	int (*dev_close)(struct Fd *);
	int (*dev_stat)(struct Fd *, struct Stat *);
	int (*dev_seek)(struct Fd *, u_int);
};

// file descriptor
// 文件描述符，作为用户程序管理、操作文件的基础。
// 不反映文件的结构，是在用户侧对文件的抽象
// 文件描述符并不是在用户进程中被创建的，而是在文件系统服务进程中创建，被共享到用户进程的地址区域中的
struct Fd {
  // 文件对应的设备id
	u_int fd_dev_id;
  // 文件读写的偏移量
	u_int fd_offset;
  // 文件读写的模式
	u_int fd_omode;
};

// State
struct Stat {
	char st_name[MAXNAMELEN];
	u_int st_size;
	u_int st_isdir;
	struct Dev *st_dev;
};

// file descriptor + file
struct Filefd {
	struct Fd f_fd;
	u_int f_fileid;
	struct File f_file;
};

int fd_alloc(struct Fd **fd);
int fd_lookup(int fdnum, struct Fd **fd);
void *fd2data(struct Fd *);
int fd2num(struct Fd *);
int dev_lookup(int dev_id, struct Dev **dev);
int num2fd(int fd);
extern struct Dev devcons;
extern struct Dev devfile;
extern struct Dev devpipe;

#endif
