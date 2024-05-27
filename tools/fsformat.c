#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PAGE_SIZE 4096
#include "../user/include/fs.h"

// 用于创建文件系统镜像的工具

/* Static assert, for compile-time assertion checking */
#define static_assert(c) (void)(char(*)[(c) ? 1 : -1])0

#define nelem(x) (sizeof(x) / sizeof((x)[0]))
typedef struct Super Super;
typedef struct File File;

#define NBLOCK 1024 // The number of blocks in the disk.

// 所需位图块的数量
uint32_t nbitblock; // the number of bitmap blocks.
uint32_t nextbno;   // next availiable block.

struct Super super; // super block.

// 磁盘块的类型
enum {
  BLOCK_FREE = 0,
  BLOCK_BOOT = 1,
  BLOCK_BMAP = 2,
  BLOCK_SUPER = 3,
  BLOCK_DATA = 4,
  BLOCK_FILE = 5,
  BLOCK_INDEX = 6,
};

// 磁盘块是一个虚拟概念，是操作系统与磁盘交互的最小单位
struct Block {
  uint8_t data[BLOCK_SIZE];
  uint32_t type;
} disk[NBLOCK];

// reverse: mutually transform between little endian and big endian.
// 进行大小尾端转换
void reverse(uint32_t *p) {
  // 通过指针数组简化操作
  uint8_t *x = (uint8_t *)p;
  uint32_t y = *(uint32_t *)x;
  x[3] = y & 0xFF;
  x[2] = (y >> 8) & 0xFF;
  x[1] = (y >> 16) & 0xFF;
  x[0] = (y >> 24) & 0xFF;
}

// reverse_block: reverse proper filed in a block.
void reverse_block(struct Block *b) {
  int i, j;
  struct Super *s;
  struct File *f, *ff;
  uint32_t *u;

  switch (b->type) {
  case BLOCK_FREE:
  case BLOCK_BOOT:
    break; // do nothing.
  case BLOCK_SUPER:
    s = (struct Super *)b->data;
    reverse(&s->s_magic);
    reverse(&s->s_nblocks);

    ff = &s->s_root;
    reverse(&ff->f_size);
    reverse(&ff->f_type);
    for (i = 0; i < NDIRECT; ++i) {
      reverse(&ff->f_direct[i]);
    }
    reverse(&ff->f_indirect);
    break;
  case BLOCK_FILE:
    f = (struct File *)b->data;
    for (i = 0; i < FILE2BLK; ++i) {
      ff = f + i;
      if (ff->f_name[0] == 0) {
        break;
      } else {
        reverse(&ff->f_size);
        reverse(&ff->f_type);
        for (j = 0; j < NDIRECT; ++j) {
          reverse(&ff->f_direct[j]);
        }
        reverse(&ff->f_indirect);
      }
    }
    break;
  case BLOCK_INDEX:
  case BLOCK_BMAP:
    u = (uint32_t *)b->data;
    for (i = 0; i < BLOCK_SIZE / 4; ++i) {
      reverse(u + i);
    }
    break;
  }
}

// 磁盘初始化，对位图和超级块进行设置，将所有的块都标为空闲块
void init_disk() {
  int i, diff;

  // 当作引导扇区和分区表使用
  disk[0].type = BLOCK_BOOT;

  // 为了使用位图管理所需位图块的数量
  nbitblock = (NBLOCK + BLOCK_SIZE_BIT - 1) / BLOCK_SIZE_BIT;

  nextbno = 2 + nbitblock;

  // 将所有的块都标为空闲块
  for (i = 0; i < nbitblock; ++i) {
    disk[2 + i].type = BLOCK_BMAP;
  }

  // 将位图中的每一个字节都设成0xff，即将所有位图块的每一位都设为1，表示磁盘块处于空闲状态
  for (i = 0; i < nbitblock; ++i) {
    memset(disk[2 + i].data, 0xff, BLOCK_SIZE);
  }

  // 如果位图还有剩余，不能将最后一块位图块中靠后的一部分内容标记为空闲，因为这些位所对应的磁盘块并不存在，是不可使用的。
  // 将所有的位图块的每一位都置为1后，根据实际情况，将位图不存在的部分设为0
  if (NBLOCK != nbitblock * BLOCK_SIZE_BIT) {
    diff = NBLOCK % BLOCK_SIZE_BIT / 8;
    memset(disk[2 + (nbitblock - 1)].data + diff, 0x00, BLOCK_SIZE - diff);
  }

  // Step 3: Initialize super block.
  disk[1].type = BLOCK_SUPER;
  super.s_magic = FS_MAGIC;
  super.s_nblocks = NBLOCK;
  super.s_root.f_type = FTYPE_DIR;
  strcpy(super.s_root.f_name, "/");
}

// Get next block id, and set `type` to the block's type.
int next_block(int type) {
  disk[nextbno].type = type;
  return nextbno++;
}

// Flush disk block usage to bitmap.
void flush_bitmap() {
  int i;
  // update bitmap, mark all bit where corresponding block is used.
  for (i = 0; i < nextbno; ++i) {
    ((uint32_t *)disk[2 + i / BLOCK_SIZE_BIT].data)[(i % BLOCK_SIZE_BIT) / 32] &=
        ~(1 << (i % 32));
  }
}

// Finish all work, dump block array into physical file.
void finish_fs(char *name) {
  int fd, i;

  // Prepare super block.
  memcpy(disk[1].data, &super, sizeof(super));

  // Dump data in `disk` to target image file.
  fd = open(name, O_RDWR | O_CREAT, 0666);
  for (i = 0; i < 1024; ++i) {
#ifdef CONFIG_REVERSE_ENDIAN
    reverse_block(disk + i);
#endif
    ssize_t n = write(fd, disk[i].data, BLOCK_SIZE);
    assert(n == BLOCK_SIZE);
  }

  // Finish.
  close(fd);
}

// Save block link.
void save_block_link(struct File *f, int block_num, int bno) {
  assert(block_num < NINDIRECT); // if not, file is too large !

  if (block_num < NDIRECT) {
    f->f_direct[block_num] = bno;
  } else {
    if (f->f_indirect == 0) {
      // create new indirect block.
      f->f_indirect = next_block(BLOCK_INDEX);
    }
    ((uint32_t *)(disk[f->f_indirect].data))[block_num] = bno;
  }
}

// Make new block contains link to files in a directory.
int make_link_block(struct File *dictionary_file, int block_num) {
  int bno = next_block(BLOCK_FILE);
  save_block_link(dictionary_file, block_num, bno);
  dictionary_file->f_size += BLOCK_SIZE;
  return bno;
}

// Overview:
//  Allocate an unused 'struct File' under the specified directory.
//
//  Note that when we delete a file, we do not re-arrange all
//  other 'File's, so we should reuse existing unused 'File's here.
//
// Post-Condition:
//  Return a pointer to an unused 'struct File'.
//  We assume that this function will never fail.
//
// Hint:
//  Use 'make_link_block' to allocate a new block for the directory if there are no existing unused
//  'File's.
// 在指定的目录下创建磁盘镜像文件，并返回相应的文件指针
struct File *create_file(struct File *dictionary_file) {
  int block_num = dictionary_file->f_size / BLOCK_SIZE;

  // Step 1: Iterate through all existing blocks in the directory.
  // 遍历目录下的所有磁盘块
  for (int i = 0; i < block_num; ++i) {
    // 磁盘块号
    int block_no;
    // 如果i在直接指针区域，则直接获取
    if (i < NDIRECT) {
      block_no = dictionary_file->f_direct[i];
    } 
    // 否则访问间接指针指向的磁盘块，通过其存储的指针获取
    else {
      block_no = ((uint32_t *)(disk[dictionary_file->f_indirect].data))[i];
    }

    // 通过磁盘块号获取对应的文件
    struct File *blk = (struct File *)(disk[block_no].data);

    // Iterate through all 'File's in the directory block.
    for (struct File *f = blk; f < blk + FILE2BLK; ++f) {
      // 如果文件名为 NULL，则代表文件未被使用，返回未被使用的文件块
      if (f->f_name[0] == NULL) {
        return f;
      }
    }
  }

  // Step 2: If no unused file is found, allocate a new block using 'make_link_block' function
  // and return a pointer to the new block on 'disk'.
  // 所有的已分配给当前目录文件、用于存储文件控制块的磁盘块都被占满了
  // 新申请一个磁盘块。该磁盘块中第一个文件控制块的位置就代表了新创建的文件。
  return (struct File *)(disk[make_link_block(dictionary_file, block_num)].data);
}

// Write file to disk under specified dir.
void write_file(struct File *dictionary_file, const char *path) {
  int iblk = 0, r = 0, n = sizeof(disk[0].data);
  struct File *target = create_file(dictionary_file);

  /* in case `create_file` is't filled */
  if (target == NULL) {
    return;
  }

  int fd = open(path, O_RDONLY);

  // Get file name with no path prefix.
  const char *fname = strrchr(path, '/');
  if (fname) {
    fname++;
  } else {
    fname = path;
  }
  strcpy(target->f_name, fname);

  target->f_size = lseek(fd, 0, SEEK_END);
  target->f_type = FTYPE_REG;

  // Start reading file.
  lseek(fd, 0, SEEK_SET);
  while ((r = read(fd, disk[nextbno].data, n)) > 0) {
    save_block_link(target, iblk++, next_block(BLOCK_DATA));
  }
  close(fd); // Close file descriptor.
}

// Overview:
//  Write directory to disk under specified dir.
//  Notice that we may use POSIX library functions to operate on
//  directory to get file infomation.
//
// Post-Condition:
//  We ASSUME that this funcion will never fail
// 用于递归地将目录下所有文件写入磁盘
void write_directory(struct File *dictionary_file, char *path) {
  DIR *dir = opendir(path);
  if (dir == NULL) {
    perror("opendir");
    return;
  }
  struct File *pdir = create_file(dictionary_file);
  strncpy(pdir->f_name, basename(path), MAXNAMELEN - 1);
  if (pdir->f_name[MAXNAMELEN - 1] != 0) {
    fprintf(stderr, "file name is too long: %s\n", path);
    // File already created, no way back from here.
    exit(1);
  }
  pdir->f_type = FTYPE_DIR;
  for (struct dirent *e; (e = readdir(dir)) != NULL;) {
    if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
      char *buf = malloc(strlen(path) + strlen(e->d_name) + 2);
      sprintf(buf, "%s/%s", path, e->d_name);
      if (e->d_type == DT_DIR) {
        write_directory(pdir, buf);
      } else {
        write_file(pdir, buf);
      }
      free(buf);
    }
  }
  closedir(dir);
}

int main(int argc, char **argv) {
  static_assert(sizeof(struct File) == FILE_STRUCT_SIZE);
  init_disk();

  if (argc < 3) {
    fprintf(stderr, "Usage: fsformat <img-file> [files or directories]...\n");
    exit(1);
  }

  for (int i = 2; i < argc; i++) {
    char *name = argv[i];
    struct stat stat_buf;
    int r = stat(name, &stat_buf);
    assert(r == 0);
    if (S_ISDIR(stat_buf.st_mode)) {
      printf("writing directory '%s' recursively into disk\n", name);
      write_directory(&super.s_root, name);
    } else if (S_ISREG(stat_buf.st_mode)) {
      printf("writing regular file '%s' into disk\n", name);
      write_file(&super.s_root, name);
    } else {
      fprintf(stderr, "'%s' has illegal file mode %o\n", name, stat_buf.st_mode);
      exit(2);
    }
  }

  flush_bitmap();
  finish_fs(argv[1]);

  return 0;
}
