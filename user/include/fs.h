#ifndef _FS_H_
#define _FS_H_ 1

#include <stdint.h>

#define FMODE_R 0x4
#define FMODE_W 0x2
#define FMODE_X 0x1
#define FMODE_RW 0x6
#define FMODE_ALL 0x7

#define STMODE2FMODE(st_mode) (((st_mode)>>6) & FMODE_ALL)

// File nodes (both in-memory and on-disk)

// Bytes per file system block - same as page size
// 磁盘块的大小，单位为字节
#define BLOCK_SIZE PAGE_SIZE
// 磁盘块的大小，单位为位
#define BLOCK_SIZE_BIT (BLOCK_SIZE * 8)

// Maximum size of a filename (a single path component), including null
// 最大文件名长度
#define MAXNAMELEN 128

// Maximum size of a complete pathname, including null
// 最大路径长度
#define MAXPATHLEN 1024

// Number of (direct) block pointers in a File descriptor
// 文件控制块的直接指针个数
#define NDIRECT 10
// 文件占据的最大磁盘块数（不直接指针个数）：一个磁盘块能容纳的最多指针数
#define NINDIRECT (BLOCK_SIZE / 4)
// 文件的最大大小
#define MAXFILESIZE (NINDIRECT * BLOCK_SIZE)

#define FILE_STRUCT_SIZE 256

// 文件控制块
struct File {
  // 文件名，最大长度为128
  char f_name[MAXNAMELEN];
  // 文件大小，单位为字节
  uint32_t f_size;
  // 文件类型，有普通文件FTYPE_REG和目录FTYPE_DIR两种。
  uint32_t f_type;
  // 用来记录存储文件的磁盘块的磁盘控制块id
  // 最多存储10个磁盘控制块id，每个磁盘块大小为4KB
  uint32_t f_direct[NDIRECT];
  // 用于存储 更多的磁盘块指针 的磁盘块的磁盘控制块id
  // 在文件大小超过40KB时使用，共1024个指针，但不使用前10个指针
  uint32_t f_indirect;
  // 指向文件所属的文件目录
  struct File *f_dir;
 
  uint32_t f_mode;
  // 让文件控制块和PAGE_SIZE对齐的填充部分
  char f_pad[FILE_STRUCT_SIZE - MAXNAMELEN - (4 + NDIRECT) * 4 - sizeof(void *)];
} __attribute__((aligned(4), packed));

// 一个磁盘块拥有的文件控制块数目
#define FILE2BLK (BLOCK_SIZE / sizeof(struct File))

// 常规文件类型
#define FTYPE_REG 0
// 目录类型
#define FTYPE_DIR 1

// File system super-block (both in-memory and on-disk)

#define FS_MAGIC 0x68286097 // Everyone's favorite OS class

// 超级块，用于描述文件系统的基本信息，如Magic Number、磁盘大小以及根目录的位置。
struct Super {
  // 魔数，用于标识该文件系统。
  uint32_t s_magic;
  // 记录本文件系统有多少个磁盘块，本文件系统中为1024
  uint32_t s_nblocks;
  // 根目录节点，根目录的f_type为FTYPE_DIR，f_name为 “/”
  struct File s_root;
};

#endif // _FS_H_
