#ifndef _FS_H_
#define _FS_H_ 1

#include <stdint.h>

// File nodes (both in-memory and on-disk)

// Bytes per file system block - same as page size
// 磁盘块的大小，单位为字节
#define BLOCK_SIZE PAGE_SIZE
#define BLOCK_SIZE_BIT (BLOCK_SIZE * 8)

// Maximum size of a filename (a single path component), including null
#define MAXNAMELEN 128

// Maximum size of a complete pathname, including null
#define MAXPATHLEN 1024

// Number of (direct) block pointers in a File descriptor
#define NDIRECT 10
#define NINDIRECT (BLOCK_SIZE / 4)

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
  // 文件的直接指针，用来记录存储文件的磁盘块
  // 最多有10个指针，每个磁盘块大小为4KB，故文件最大为40KB
  uint32_t f_direct[NDIRECT];
  // 指向一个间接磁盘块，用来存储  指向文件内容的磁盘块的指针
  // 在文件大小超过40KB时使用，共1024个指针，但不使用前10个指针
  uint32_t f_indirect;
  // 指向文件所属的文件目录
  struct File *f_dir;
  // 为了让整数个文件结构体占用一个磁盘块，填充结构体中剩下的字节
  char f_pad[FILE_STRUCT_SIZE - MAXNAMELEN - (3 + NDIRECT) * 4 - sizeof(void *)];
} __attribute__((aligned(4), packed));

#define FILE2BLK (BLOCK_SIZE / sizeof(struct File))

// File types
#define FTYPE_REG 0 // Regular file
#define FTYPE_DIR 1 // Directory

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
