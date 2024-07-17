#include <fs.h>
#include <lib.h>
#include <mmu.h>

#define PTE_DIRTY 0x0004 // file system block cache is dirty

// 扇区的大小，单位为字节
#define SECT_SIZE 512
// 每个磁盘块所含有的扇区数量
// 扇区是实际的存储单位，只有512B，太小了，所以用磁盘块来进行逻辑操作的基本的单位
#define SECT2BLK (BLOCK_SIZE / SECT_SIZE)

/* Disk block n, when in memory, is mapped into the file system
 * server's address space at DISKMAP+(n*BLOCK_SIZE). */
// 磁盘块缓存区域的起始虚拟地址
#define DISKMAP 0x10000000

/* Maximum disk size we can handle (1GB) */
// 磁盘块缓存区域的终止虚拟地址
#define DISKMAX 0x40000000

/* ide.c */
void ide_read(u_int diskno, u_int secno, void *dst, u_int nsecs);
void ide_write(u_int diskno, u_int secno, void *src, u_int nsecs);

/* fs.c */
int file_open(char *path, struct File **pfile);
int file_create(char *path, struct File **file);
int file_get_block(struct File *f, u_int blockno, void **pblk);
int file_set_size(struct File *f, u_int newsize);
void file_close(struct File *f);
int file_remove(char *path);
int file_dirty(struct File *f, u_int offset);
void file_flush(struct File *);

void fs_init(void);
void fs_sync(void);
extern uint32_t *bitmap;
int map_block(u_int);
int alloc_block(void);
