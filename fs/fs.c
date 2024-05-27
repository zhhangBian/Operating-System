#include "serv.h"
#include <mmu.h>

struct Super *super;

uint32_t *bitmap;

void file_flush(struct File *);
int block_is_free(u_int);

// Overview:
//  Return the virtual address of this disk block in cache.
// 获取磁盘块号映射在虚存中的地址
void *disk_addr(u_int block_no) {
  return DISKMAP + block_no * BLOCK_SIZE;
}

// Overview:
//  Check if this virtual address is mapped to a block. (check PTE_V bit)
// 检查虚拟地址是否已经存在映射关系，本质是在检查页表是否有效
int va_is_mapped(void *va) {
  return (vpd[PDX(va)] & PTE_V) && (vpt[VPN(va)] & PTE_V);
}

// Overview:
//  Check if this disk block is mapped in cache.
//  Returns the virtual address of the cache page if mapped, 0 otherwise.
// 检查磁盘是否已经在内存中分配了内存块
void *block_is_mapped(u_int block_no) {
  // 获取磁盘块在内存中对应的虚拟地址
  void *virtual_address = disk_addr(block_no);
  // 如果已经存在映射关系，则返回相应的虚拟地址
  if (va_is_mapped(virtual_address)) {
    return virtual_address;
  }

  return NULL;
}

// Overview:
//  Check if this virtual address is dirty. (check PTE_DIRTY bit)
int va_is_dirty(void *va) {
  return vpt[VPN(va)] & PTE_DIRTY;
}

// Overview:
//  Check if this block is dirty. (check corresponding `va`)
int block_is_dirty(u_int block_no) {
  void *va = disk_addr(block_no);
  return va_is_mapped(va) && va_is_dirty(va);
}

// Overview:
//  Mark this block as dirty (cache page has changed and needs to be written back to disk).
int dirty_block(u_int block_no) {
  void *va = disk_addr(block_no);

  if (!va_is_mapped(va)) {
    return -E_NOT_FOUND;
  }

  if (va_is_dirty(va)) {
    return 0;
  }

  return syscall_mem_map(0, va, 0, va, PTE_D | PTE_DIRTY);
}

// Overview:
//  Write the current contents of the block out to disk.
// 将在内存中的数据写回磁盘
void write_block(u_int block_no) {
  // 检查是否为磁盘块在内存中分配了空间
  if (!block_is_mapped(block_no)) {
    user_panic("write unmapped block %08x", block_no);
  }
  // 将磁盘块的数据写回磁盘0中
  ide_write(0, block_no * SECT2BLK, disk_addr(block_no), SECT2BLK);
}

// Overview:
//  Make sure a particular disk block is loaded into memory.
//
// Post-Condition:
//  Return 0 on success, or a negative error code on error.
//
//  If blk!=0, set *blk to the address of the block in memory.
//
//  If isnew!=0, set *isnew to 0 if the block was already in memory, or
//  to 1 if the block was loaded off disk to satisfy this request. (Isnew
//  lets callers like file_get_block clear any memory-only fields
//  from the disk blocks when they come in off disk.)

// 将指定编号的磁盘块读入到内存中，自动处理原先未分配物理内存的情况
int read_block(u_int block_no,  // 读取的磁盘块的块号
               void **block_va_pointer, // 记录磁盘块在虚拟内存的地址，NULL则不记录
               u_int *if_not_mapped_before  // 记录原先是否已经被读入内存，NULL则不记录
  ) {
  // 检查磁盘块号是否合法
  if (super && block_no >= super->s_nblocks) {
    user_panic("reading non-existent block %08x\n", block_no);
  }

  // 检查位图是否从磁盘被读取到了内存中，并检查磁盘块是否被使用，还是空闲状态
  if (bitmap && block_is_free(block_no)) {
    user_panic("reading free block %08x\n", block_no);
  }

  // 获取磁盘块在虚拟内存中的相应地址
  void *virtual_address = disk_addr(block_no);

  // 如果磁盘块原先已经被读入内存
  if (block_is_mapped(block_no)) {
    if (if_not_mapped_before) {
      *if_not_mapped_before = 0;
    }
  }
  // 如果没有，则分配内存，从磁盘中读取数据
  else {
    if (if_not_mapped_before) {
      *if_not_mapped_before = 1;
    }
    // 分配物理内存
    try(syscall_mem_alloc(0, virtual_address, PTE_D));
    // 从磁盘中读取数据
    ide_read(0, block_no * SECT2BLK, virtual_address, SECT2BLK);
  }

  // 记录磁盘块在虚拟内存的地址
  if (block_va_pointer) {
    *block_va_pointer = virtual_address;
  }
  return 0;
}

// Overview:
//  Allocate a page to cache the disk block.
// 为磁盘块在内存中分配物理内存，建立映射
int map_block(u_int block_no) {
  // 检查是否已经建立了映射
  if (block_is_mapped(block_no)) {
    return 0;
  }

  // 为磁盘在内存中分配物理内存，权限设置为可写
  try(syscall_mem_alloc(env->env_id, disk_addr(block_no), PTE_D));
}

// Overview:
//  Unmap a disk block in cache.
// 取消原先给磁盘分配的物理内存
void unmap_block(u_int block_no) {
  // Step 1: Get the mapped address of the cache page of this block using 'block_is_mapped'.
  void *virtual_address;
  virtual_address = block_is_mapped(block_no);

  // 如果磁盘被使用，并且相应数据被修改，先将修改数据写回磁盘
  if (!block_is_free(block_no) && block_is_dirty(block_no)) {
    write_block(block_no);
  }

  // 通过系统调用取消原先的映射关系
  syscall_mem_unmap(env->env_id, disk_addr(block_no));
  // 检查是否真的已经取消了映射关系
  user_assert(!block_is_mapped(block_no));
}

// Overview:
//  Check if the block 'block_no' is free via bitmap.
//
// Post-Condition:
//  Return 1 if the block is free, else 0.
// 根据位图来判断指定的磁盘块是否被占用
int block_is_free(u_int block_no) {
  // 判断磁盘块号是否合法
  if (super == 0 || block_no >= super->s_nblocks) {
    return 0;
  }
  // 位图为1代表空闲
  if (bitmap[block_no / 32] & (1 << (block_no % 32))) {
    return 1;
  }

  return 0;
}

// Overview:
//  Mark a block as free in the bitmap.
// 通过位图设置一个磁盘块为空闲
void free_block(u_int block_no) {
  // 判断磁盘块号是否合法
  if (super == 0 || block_no >= super->s_nblocks) {
    return;
  }

  // 设置位图为1，将磁盘块标记为空闲
  bitmap[block_no / 32] |= 1 << (block_no % 32);
}

// Overview:
//  Search in the bitmap for a free block and allocate it.
//
// Post-Condition:
//  Return block number allocated on success,
//  Return -E_NO_DISK if we are out of blocks.
int alloc_block_num(void) {
  int block_no;
  // walk through this bitmap, find a free one and mark it as used, then sync
  // this block to IDE disk (using `write_block`) from memory.
  for (block_no = 3; block_no < super->s_nblocks; block_no++) {
    if (bitmap[block_no / 32] & (1 << (block_no % 32))) { // the block is free
      bitmap[block_no / 32] &= ~(1 << (block_no % 32));
      write_block(block_no / BLOCK_SIZE_BIT + 2); // write to disk.
      return block_no;
    }
  }
  // no free blocks.
  return -E_NO_DISK;
}

// Overview:
//  Allocate a block -- first find a free block in the bitmap, then map it into memory.
int alloc_block(void) {
  int r, bno;
  // Step 1: find a free block.
  if ((r = alloc_block_num()) < 0) { // failed.
    return r;
  }
  bno = r;

  // Step 2: map this block into memory.
  if ((r = map_block(bno)) < 0) {
    free_block(bno);
    return r;
  }

  // Step 3: return block number.
  return bno;
}

// Overview:
//  Read and validate the file system super-block.
//
// Post-condition:
//  If error occurred during read super block or validate failed, panic.
void read_super(void) {
  int r;
  void *blk;

  // Step 1: read super block.
  if ((r = read_block(1, &blk, 0)) < 0) {
    user_panic("cannot read superblock: %d", r);
  }

  super = blk;

  // Step 2: Check fs magic nunber.
  if (super->s_magic != FS_MAGIC) {
    user_panic("bad file system magic number %x %x", super->s_magic, FS_MAGIC);
  }

  // Step 3: validate disk size.
  if (super->s_nblocks > DISKMAX / BLOCK_SIZE) {
    user_panic("file system is too large");
  }

  debugf("superblock is good\n");
}

// Overview:
//  Read and validate the file system bitmap.
//
// Hint:
//  Read all the bitmap blocks into memory.
//  Set the 'bitmap' to point to the first bitmap block.
//  For each block i, user_assert(!block_is_free(i))) to check that they're all marked as in use.
void read_bitmap(void) {
  u_int i;
  void *blk = NULL;

  // Step 1: Calculate the number of the bitmap blocks, and read them into memory.
  u_int nbitmap = super->s_nblocks / BLOCK_SIZE_BIT + 1;
  for (i = 0; i < nbitmap; i++) {
    read_block(i + 2, blk, 0);
  }

  bitmap = disk_addr(2);

  // Step 2: Make sure the reserved and root blocks are marked in-use.
  // Hint: use `block_is_free`
  user_assert(!block_is_free(0));
  user_assert(!block_is_free(1));

  // Step 3: Make sure all bitmap blocks are marked in-use.
  for (i = 0; i < nbitmap; i++) {
    user_assert(!block_is_free(i + 2));
  }

  debugf("read_bitmap is good\n");
}

// Overview:
//  Test that write_block works, by smashing the superblock and reading it back.
void check_write_block(void) {
  super = 0;

  // backup the super block.
  // copy the data in super block to the first block on the disk.
  panic_on(read_block(0, 0, 0));
  memcpy((char *)disk_addr(0), (char *)disk_addr(1), BLOCK_SIZE);

  // smash it
  strcpy((char *)disk_addr(1), "OOPS!\n");
  write_block(1);
  user_assert(block_is_mapped(1));

  // clear it out
  panic_on(syscall_mem_unmap(0, disk_addr(1)));
  user_assert(!block_is_mapped(1));

  // validate the data read from the disk.
  panic_on(read_block(1, 0, 0));
  user_assert(strcmp((char *)disk_addr(1), "OOPS!\n") == 0);

  // restore the super block.
  memcpy((char *)disk_addr(1), (char *)disk_addr(0), BLOCK_SIZE);
  write_block(1);
  super = (struct Super *)disk_addr(1);
}

// Overview:
//  Initialize the file system.
// Hint:
//  1. read super block.
//  2. check if the disk can work.
//  3. read bitmap blocks from disk to memory.
void fs_init(void) {
  read_super();
  check_write_block();
  read_bitmap();
}

// Overview:
//  Like pgdir_walk but for files.
//  Find the disk block number slot for the 'filebno'th block in file 'f'. Then, set
//  '*ppdiskbno' to point to that slot. The slot will be one of the f->f_direct[] entries,
//  or an entry in the indirect block.
//  When 'alloc' is set, this function will allocate an indirect block if necessary.
//
// Post-Condition:
//  Return 0 on success, and set *ppdiskbno to the pointer to the target block.
//  Return -E_NOT_FOUND if the function needed to allocate an indirect block, but alloc was 0.
//  Return -E_NO_DISK if there's no space on the disk for an indirect block.
//  Return -E_NO_MEM if there's not enough memory for an indirect block.
//  Return -E_INVAL if filebno is out of range (>= NINDIRECT).
int file_block_walk(struct File *f, u_int filebno, uint32_t **ppdiskbno, u_int alloc) {
  int r;
  uint32_t *ptr;
  uint32_t *blk;

  if (filebno < NDIRECT) {
    // Step 1: if the target block is corresponded to a direct pointer, just return the
    // disk block number.
    ptr = &f->f_direct[filebno];
  } else if (filebno < NINDIRECT) {
    // Step 2: if the target block is corresponded to the indirect block, but there's no
    //  indirect block and `alloc` is set, create the indirect block.
    if (f->f_indirect == 0) {
      if (alloc == 0) {
        return -E_NOT_FOUND;
      }

      if ((r = alloc_block()) < 0) {
        return r;
      }
      f->f_indirect = r;
    }

    // Step 3: read the new indirect block to memory.
    if ((r = read_block(f->f_indirect, (void **)&blk, 0)) < 0) {
      return r;
    }
    ptr = blk + filebno;
  } else {
    return -E_INVAL;
  }

  // Step 4: store the result into *ppdiskbno, and return 0.
  *ppdiskbno = ptr;
  return 0;
}

// OVerview:
//  Set *diskbno to the disk block number for the filebno'th block in file f.
//  If alloc is set and the block does not exist, allocate it.
//
// Post-Condition:
//  Returns 0: success, < 0 on error.
//  Errors are:
//   -E_NOT_FOUND: alloc was 0 but the block did not exist.
//   -E_NO_DISK: if a block needed to be allocated but the disk is full.
//   -E_NO_MEM: if we're out of memory.
//   -E_INVAL: if filebno is out of range.
int file_map_block(struct File *f, u_int filebno, u_int *diskbno, u_int alloc) {
  int r;
  uint32_t *ptr;

  // Step 1: find the pointer for the target block.
  if ((r = file_block_walk(f, filebno, &ptr, alloc)) < 0) {
    return r;
  }

  // Step 2: if the block not exists, and create is set, alloc one.
  if (*ptr == 0) {
    if (alloc == 0) {
      return -E_NOT_FOUND;
    }

    if ((r = alloc_block()) < 0) {
      return r;
    }
    *ptr = r;
  }

  // Step 3: set the pointer to the block in *diskbno and return 0.
  *diskbno = *ptr;
  return 0;
}

// Overview:
//  Remove a block from file f. If it's not there, just silently succeed.
int file_clear_block(struct File *f, u_int filebno) {
  int r;
  uint32_t *ptr;

  if ((r = file_block_walk(f, filebno, &ptr, 0)) < 0) {
    return r;
  }

  if (*ptr) {
    free_block(*ptr);
    *ptr = 0;
  }

  return 0;
}

// Overview:
//  Set *blk to point at the filebno'th block in file f.
//
// Hint: use file_map_block and read_block.
//
// Post-Condition:
//  return 0 on success, and read the data to `blk`, return <0 on error.
// 获取文件对应的磁盘块
int file_get_block(struct File *f, u_int filebno, void **blk) {
  int func_info;
  u_int diskbno;
  u_int isnew;

  // Step 1: find the disk block number is `f` using `file_map_block`.
  // 为即将读入内存的磁盘块分配物理内存
  if ((func_info = file_map_block(f, filebno, &diskbno, 1)) < 0) {
    return func_info;
  }

  // Step 2: read the data in this disk to blk.
  // 将磁盘内容以块为单位读入内存中的相应位置
  if ((func_info = read_block(diskbno, blk, &isnew)) < 0) {
    return func_info;
  }

  return 0;
}

// Overview:
//  Mark the offset/BLOCK_SIZE'th block dirty in file f.
int file_dirty(struct File *f, u_int offset) {
  int r;
  u_int diskbno;

  if ((r = file_map_block(f, offset / BLOCK_SIZE, &diskbno, 0)) < 0) {
    return r;
  }

  return dirty_block(diskbno);
}

// Overview:
//  Find a file named 'name' in the directory 'dir'. If found, set *file to it.
//
// Post-Condition:
//  Return 0 on success, and set the pointer to the target file in `*file`.
//  Return the underlying error if an error occurs.
// 查找某个目录下是否存在指定的文件
int dir_lookup(struct File *dictionary, char *name, struct File **file) {
  // 计算目录下的磁盘块数
  u_int block_num = dictionary->f_size / PAGE_SIZE;

  // Step 2: Iterate through all blocks in the directory.
  for (int i = 0; i < block_num; i++) {
    // Read the i'th block of 'dir' and get its address in 'blk' using 'file_get_block'.
    void *blk;
    try(file_get_block(dictionary, i, &blk));

    struct File *files = (struct File *)blk;

    // Find the target among all 'File's in this block.
    for (struct File *f = files; f < files + FILE2BLK; ++f) {
      // Compare the file name against 'name' using 'strcmp'.
      // If we find the target file, set '*file' to it and set up its 'f_dir'
      // field.
      /* Exercise 5.8: Your code here. (3/3) */
      if (strcmp(name, f->f_name) == 0) {
        *file = f;
        f->f_dir = dictionary;
        return 0;
      }
    }
  }

  return -E_NOT_FOUND;
}

// Overview:
//  Alloc a new File structure under specified directory. Set *file
//  to point at a free File structure in dir.
int dir_alloc_file(struct File *dir, struct File **file) {
  int r;
  u_int nblock, i, j;
  void *blk;
  struct File *f;

  nblock = dir->f_size / BLOCK_SIZE;

  for (i = 0; i < nblock; i++) {
    // read the block.
    if ((r = file_get_block(dir, i, &blk)) < 0) {
      return r;
    }

    f = blk;

    for (j = 0; j < FILE2BLK; j++) {
      if (f[j].f_name[0] == '\0') { // found free File structure.
        *file = &f[j];
        return 0;
      }
    }
  }

  // no free File structure in exists data block.
  // new data block need to be created.
  dir->f_size += BLOCK_SIZE;
  if ((r = file_get_block(dir, i, &blk)) < 0) {
    return r;
  }
  f = blk;
  *file = &f[0];

  return 0;
}

// Overview:
//  Skip over slashes.
char *skip_slash(char *p) {
  while (*p == '/') {
    p++;
  }
  return p;
}

// Overview:
//  Evaluate a path name, starting at the root.
//
// Post-Condition:
//  On success, set *pfile to the file we found and set *pdir to the directory
//  the file is in.
//  If we cannot find the file but find the directory it should be in, set
//  *pdir and copy the final path element into lastelem.
int walk_path(char *path, struct File **pdir, struct File **pfile, char *lastelem) {
  char *p;
  char name[MAXNAMELEN];
  struct File *dir, *file;
  int r;

  // start at the root.
  path = skip_slash(path);
  file = &super->s_root;
  dir = 0;
  name[0] = 0;

  if (pdir) {
    *pdir = 0;
  }

  *pfile = 0;

  // find the target file by name recursively.
  while (*path != '\0') {
    dir = file;
    p = path;

    while (*path != '/' && *path != '\0') {
      path++;
    }

    if (path - p >= MAXNAMELEN) {
      return -E_BAD_PATH;
    }

    memcpy(name, p, path - p);
    name[path - p] = '\0';
    path = skip_slash(path);
    if (dir->f_type != FTYPE_DIR) {
      return -E_NOT_FOUND;
    }

    if ((r = dir_lookup(dir, name, &file)) < 0) {
      if (r == -E_NOT_FOUND && *path == '\0') {
        if (pdir) {
          *pdir = dir;
        }

        if (lastelem) {
          strcpy(lastelem, name);
        }

        *pfile = 0;
      }

      return r;
    }
  }

  if (pdir) {
    *pdir = dir;
  }

  *pfile = file;
  return 0;
}

// Overview:
//  Open "path".
//
// Post-Condition:
//  On success set *pfile to point at the file and return 0.
//  On error return < 0.
int file_open(char *path, struct File **file) {
  return walk_path(path, 0, file, 0);
}

// Overview:
//  Create "path".
//
// Post-Condition:
//  On success set *file to point at the file and return 0.
//  On error return < 0.
int file_create(char *path, struct File **file) {
  char name[MAXNAMELEN];
  int r;
  struct File *dir, *f;

  if ((r = walk_path(path, &dir, &f, name)) == 0) {
    return -E_FILE_EXISTS;
  }

  if (r != -E_NOT_FOUND || dir == 0) {
    return r;
  }

  if (dir_alloc_file(dir, &f) < 0) {
    return r;
  }

  strcpy(f->f_name, name);
  *file = f;
  return 0;
}

// Overview:
//  Truncate file down to newsize bytes.
//
//  Since the file is shorter, we can free the blocks that were used by the old
//  bigger version but not by our new smaller self. For both the old and new sizes,
//  figure out the number of blocks required, and then clear the blocks from
//  new_nblocks to old_nblocks.
//
//  If the new_nblocks is no more than NDIRECT, free the indirect block too.
//  (Remember to clear the f->f_indirect pointer so you'll know whether it's valid!)
//
// Hint: use file_clear_block.
void file_truncate(struct File *f, u_int newsize) {
  u_int bno, old_nblocks, new_nblocks;

  old_nblocks = ROUND(f->f_size, BLOCK_SIZE) / BLOCK_SIZE;
  new_nblocks = ROUND(newsize, BLOCK_SIZE) / BLOCK_SIZE;

  if (newsize == 0) {
    new_nblocks = 0;
  }

  if (new_nblocks <= NDIRECT) {
    for (bno = new_nblocks; bno < old_nblocks; bno++) {
      panic_on(file_clear_block(f, bno));
    }
    if (f->f_indirect) {
      free_block(f->f_indirect);
      f->f_indirect = 0;
    }
  } else {
    for (bno = new_nblocks; bno < old_nblocks; bno++) {
      panic_on(file_clear_block(f, bno));
    }
  }
  f->f_size = newsize;
}

// Overview:
//  Set file size to newsize.
int file_set_size(struct File *f, u_int newsize) {
  if (f->f_size > newsize) {
    file_truncate(f, newsize);
  }

  f->f_size = newsize;

  if (f->f_dir) {
    file_flush(f->f_dir);
  }

  return 0;
}

// Overview:
//  Flush the contents of file f out to disk.
//  Loop over all the blocks in file.
//  Translate the file block number into a disk block number and then
//  check whether that disk block is dirty. If so, write it out.
//
// Hint: use file_map_block, block_is_dirty, and write_block.
void file_flush(struct File *f) {
  u_int nblocks;
  u_int bno;
  u_int diskno;
  int r;

  nblocks = ROUND(f->f_size, BLOCK_SIZE) / BLOCK_SIZE;

  for (bno = 0; bno < nblocks; bno++) {
    if ((r = file_map_block(f, bno, &diskno, 0)) < 0) {
      continue;
    }
    if (block_is_dirty(diskno)) {
      write_block(diskno);
    }
  }
}

// Overview:
//  Sync the entire file system.  A big hammer.
void fs_sync(void) {
  int i;
  for (i = 0; i < super->s_nblocks; i++) {
    if (block_is_dirty(i)) {
      write_block(i);
    }
  }
}

// Overview:
//  Close a file.
void file_close(struct File *f) {
  // Flush the file itself, if f's f_dir is set, flush it's f_dir.
  file_flush(f);
  if (f->f_dir) {
    file_flush(f->f_dir);
  }
}

// Overview:
//  Remove a file by truncating it and then zeroing the name.
int file_remove(char *path) {
  int r;
  struct File *f;

  // Step 1: find the file on the disk.
  if ((r = walk_path(path, 0, &f, 0)) < 0) {
    return r;
  }

  // Step 2: truncate it's size to zero.
  file_truncate(f, 0);

  // Step 3: clear it's name.
  f->f_name[0] = '\0';

  // Step 4: flush the file.
  file_flush(f);
  if (f->f_dir) {
    file_flush(f->f_dir);
  }

  return 0;
}
