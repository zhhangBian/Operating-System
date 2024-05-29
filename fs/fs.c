#include "serv.h"
#include <mmu.h>

struct Super *super;

// 管理磁盘块的位图
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
int va_is_mapped(void *virtual_address) {
  return (vpd[PDX(virtual_address)] & PTE_V) && (vpt[VPN(virtual_address)] & PTE_V);
}

// Overview:
//  Check if this disk block is mapped in cache.
//  Returns the virtual address of the cache page if mapped, 0 otherwise.
// 检查磁盘是否已经在内存中分配了内存块，如果没有返回NULL
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
// 检查虚拟地址对应的内存是否已经被修改
int va_is_dirty(void *virtual_address) {
  return vpt[VPN(virtual_address)] & PTE_DIRTY;
}

// Overview:
//  Check if this block is dirty. (check corresponding `va`)
// 检查磁盘块是否已经被修改
int block_is_dirty(u_int block_no) {
  void *virtual_address = disk_addr(block_no);
  return va_is_mapped(virtual_address) && va_is_dirty(virtual_address);
}

// Overview:
//  Mark this block as dirty (cache page has changed and needs to be written back to disk).
// 将磁盘块标记被修改
int dirty_block(u_int block_no) {
  void *virtual_address = disk_addr(block_no);

  if (!va_is_mapped(virtual_address)) {
    return -E_NOT_FOUND;
  }

  if (va_is_dirty(virtual_address)) {
    return 0;
  }
  // 标记脏位的方式是修改页面权限
  return syscall_mem_map(0, virtual_address, 0, virtual_address, PTE_D | PTE_DIRTY);
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
  void *virtual_address = disk_addr(block_no);
  // 写一个磁盘块的内容
  ide_write(0, block_no * SECT2BLK, virtual_address, SECT2BLK);
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
// 将指定编号的磁盘块读入到内存中，将内存中的虚拟地址保存到指针中
// 自动处理原先未分配物理内存的情况
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

  // 如果磁盘块原先已经被读入内存，不需要操作
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
    // 读取一整个磁盘块的内容
    // 利用乘得到读取的扇区号，读取一个磁盘块的内容
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
  // 如果已经建立了映射，则不需要再建立
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
// 通过位图设置第no个磁盘块为空闲
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
// 获得一个空闲磁盘块，返回其磁盘块号
int alloc_block_num(void) {
  int block_no;
  // 遍历位图，找到一个空闲磁盘块
  // 返回前写回磁盘块的内容到磁盘
  for (block_no = 3; block_no < super->s_nblocks; block_no++) {
    // 通过位图判断磁盘块未被使用
    if (bitmap[block_no / 32] & (1 << (block_no % 32))) {
      // 将这个磁盘块标记为在被使用
      bitmap[block_no / 32] &= ~(1 << (block_no % 32));
      // 将磁盘块的内容写回到磁盘中去
      write_block(block_no / BLOCK_SIZE_BIT + 2); // write to disk.

      return block_no;
    }
  }
  // 没有空闲的磁盘块
  return -E_NO_DISK;
}

// Overview:
//  Allocate a block -- first find a free block in the bitmap, then map it into memory.
// 找到一个空闲的磁盘块，返回对应的磁盘块号
int alloc_block(void) {
  int block_no;
  int func_info;

  // 找到一个磁盘块
  if ((block_no = alloc_block_num()) < 0) {
    return block_no;
  }

  // 将磁盘块加载到内存中：建立映射
  if ((func_info = map_block(block_no)) < 0) {
    // 如果失败，则不占用磁盘，恢复位图
    free_block(block_no);
    return func_info;
  }

  // 返回磁盘块号
  return block_no;
}

// Overview:
//  Read and validate the file system super-block.
//
// Post-condition:
//  If error occurred during read super block or validate failed, panic.
// 读入超级块到磁盘，并检查正确性
void read_super(void) {
  void *block_va;
  int func_info;

  // 将超级块读入内存，获得其地址
  if ((func_info = read_block(1, &block_va, 0)) < 0) {
    user_panic("cannot read superblock: %d", func_info);
  }
  super = block_va;

  // 检查超级块的魔数
  if (super->s_magic != FS_MAGIC) {
    user_panic("bad file system magic number %x %x", super->s_magic, FS_MAGIC);
  }
  // 检查超级块大小
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
// 读入位图至内存并检查正确性
void read_bitmap(void) {
  void *block_va = NULL;

  // 计算位图所需的磁盘块数
  u_int bitmap_block_num = super->s_nblocks / BLOCK_SIZE_BIT + 1;
  for (int i = 0; i < bitmap_block_num; i++) {
    read_block(bitmap_block_num + 2, &block_va, 0);
  }
  // 设置位图的地址
  bitmap = disk_addr(2);

  // 检查跟和超级快的使用情况
  user_assert(!block_is_free(0));
  user_assert(!block_is_free(1));

  // 确定位图所有所需块被载入内存
  for (int i = 0; i < bitmap_block_num; i++) {
    user_assert(!block_is_free(i + 2));
  }

  debugf("read_bitmap is good\n");
}

// Overview:
//  Test that write_block works, by smashing the superblock and reading it back.
// 检查磁盘能否工作
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
  // 检查超级块
  read_super();
  // 检查磁盘能否工作
  check_write_block();
  // 检查位图
  read_bitmap();
}

// Overview:
//  Like pgdir_walk but for files.
//  Find the disk block number slot for the 'file_block_no'th block in file 'f'.
//  Then, set '*ppdiskblock_no' to point to that slot.
//  The slot will be one of the f->f_direct[] entries, or an entry in the indirect block.
//  When 'alloc' is set, this function will allocate an indirect block if necessary.
//
// Post-Condition:
//  Return 0 on success, and set *ppdiskblock_no to the pointer to the target block.
//  Return -E_NOT_FOUND if the function needed to allocate an indirect block, but alloc was 0.
//  Return -E_NO_DISK if there's no space on the disk for an indirect block.
//  Return -E_NO_MEM if there's not enough memory for an indirect block.
//  Return -E_INVAL if file_block_no is out of range (>= NINDIRECT).
// 找到文件的第f_no个磁盘块，将文件控制块中存磁盘块号的地址保存到指针中
// 按照alloc设置加载到内存中
int file_block_walk(struct File *file,  // 操作的文件
                    u_int file_block_no,  // 文件的第f_no个磁盘块
                    uint32_t **block_no_pointer_pointer,  // 处理对应磁盘块号的指针
                    u_int alloc // 如果没有磁盘块是否需要分配
  ) {
  uint32_t *block_no_pointer;
  uint32_t *block_va;
  int func_info;

  // 磁盘块数由直接指针控制
  if (file_block_no < NDIRECT) {
    // 将指针指向对应的磁盘块号
    block_no_pointer = &file->f_direct[file_block_no];
  }
  // 由间接指针控制
  else if (file_block_no < NINDIRECT) {
    // 如果没有保存间接指针的磁盘块
    // 按alloc获取
    if (file->f_indirect == 0) {
      // 不需要获取磁盘块，报错
      if (alloc == 0) {
        return -E_NOT_FOUND;
      }

      // 获取一个磁盘块用作间接指针区域
      int block_no;
      if ((block_no = alloc_block()) < 0) {
        return block_no;
      }
      file->f_indirect = block_no;
    }

    // 将指定编号的磁盘块读入到内存中
    if ((func_info = read_block(file->f_indirect, (void **)&block_va, 0)) < 0) {
      return func_info;
    }
    // 保存磁盘块号 对应的指针 为 保存指针的磁盘地址+偏移量
    block_no_pointer = block_va + file_block_no;
  }
  // 错误的磁盘块号
  else {
    return -E_INVAL;
  }

  // 保存  磁盘块号所在地址  到指针中
  *block_no_pointer_pointer = block_no_pointer;
  return 0;
}

// OVerview:
//  Set *diskblock_no to the disk block number for the file_block_no'th block in file f.
//  If alloc is set and the block does not exist, allocate it.
//
// Post-Condition:
//  Returns 0: success, < 0 on error.
//  Errors are:
//   -E_NOT_FOUND: alloc was 0 but the block did not exist.
//   -E_NO_DISK: if a block needed to be allocated but the disk is full.
//   -E_NO_MEM: if we're out of memory.
//   -E_INVAL: if file_block_no is out of range.
// 获取文件块对应磁盘块号（相对文件）对应的  磁盘块号（相对磁盘）
// 如果磁盘块没有被加载到内存中，按alloc设置加载
int file_map_block(struct File *file, u_int file_block_no, u_int *disk_block_no, u_int alloc) {
  uint32_t *block_no_pointer;
  int func_info;

  // 找到文件的第f_no个磁盘块，将文件控制块中存磁盘块号的地址保存到指针中
  if ((func_info = file_block_walk(file, file_block_no, &block_no_pointer, alloc)) < 0) {
    return func_info;
  }

  // 如果磁盘块不存在，按alloc创建
  if (*block_no_pointer == 0) {
    // 不需要创建，则报错
    if (alloc == 0) {
      return -E_NOT_FOUND;
    }
    // 创建一个磁盘块供使用
    if ((func_info = alloc_block()) < 0) {
      return func_info;
    }
    *block_no_pointer = func_info;
  }

  // 将对应的结果保存到指针中
  *disk_block_no = *block_no_pointer;
  return 0;
}

// Overview:
//  Remove a block from file f. If it's not there, just silently succeed.
// 将文件的第f_no个磁盘控制块从文件控制块文件中移除
int file_clear_block(struct File *file, u_int file_block_no) {
  uint32_t *block_no_pointer;
  int func_info;

  // 获取文件的第f_no个磁盘控制块，保存到指针中
  if ((func_info = file_block_walk(file, file_block_no, &block_no_pointer, 0)) < 0) {
    return func_info;
  }

  // 磁盘块有效
  if (*block_no_pointer) {
    free_block(*block_no_pointer);
    *block_no_pointer = 0;
  }

  return 0;
}

// Overview:
//  Set *blk to point at the file_block_no'th block in file f.
//
// Hint: use file_map_block and read_block.
//
// Post-Condition:
//  return 0 on success, and read the data to `block_va_pointer`, return <0 on error.
// 获取文件第f_no个磁盘块，保存到指针中，没有则创建
int file_get_block(struct File *file, u_int file_block_no, void **block_va_pointer) {
  int func_info;
  u_int disk_block_no;
  u_int if_not_mapped_before;

  // 获取文件块对应磁盘块号（相对文件）对应的  磁盘块号（相对磁盘）
  if ((func_info = file_map_block(file, file_block_no, &disk_block_no, 1)) < 0) {
    return func_info;
  }

  // 将磁盘内容以块为单位读入内存中的相应位置
  if ((func_info = read_block(disk_block_no, block_va_pointer, &if_not_mapped_before)) < 0) {
    return func_info;
  }

  return 0;
}

// Overview:
//  Mark the offset/BLOCK_SIZE'th block dirty in file f.
// 将文件控制块标记为脏
int file_dirty(struct File *file, u_int offset) {
  // 获取offest对应的磁盘块号（相对文件）
  u_int file_block_no = offset / BLOCK_SIZE;
  u_int disk_block_no;
  int func_info;

  // 获取文件中的第f_no个磁盘块的磁盘块号
  if ((func_info = file_map_block(file, file_block_no, &disk_block_no, 0)) < 0) {
    return func_info;
  }
  // 将磁盘控制块标记为脏
  return dirty_block(disk_block_no);
}

// Overview:
//  Find a file named 'name' in the directory 'dir'. If found, set *file to it.
//
// Post-Condition:
//  Return 0 on success, and set the pointer to the target file in `*file`.
//  Return the underlying error if an error occurs.
// 在目录下查找特定名字的文件，保存到指针中
int dir_lookup(struct File *dictionary, char *name, struct File **file_pointer) {
  // 获取目录占有的总磁盘块数
  u_int block_num = dictionary->f_size / PAGE_SIZE;
  void *block;

  // 遍历目录占据的磁盘块，寻找文件控制块
  for (int i = 0; i < block_num; i++) {
    // 获取文件第i个磁盘块，保存到block指针中
    try(file_get_block(dictionary, i, &block));

    struct File *files = (struct File *)block;
    // 遍历磁盘块中的所有文件控制块，比较文件名
    for (struct File *file = files; file < files + FILE2BLK; file++) {
      // 比较文件名来判断是否为所需文件
      if (strcmp(name, file->f_name) == 0) {
        *file_pointer = file;
        // 设置文件的所属目录
        file->f_dir = dictionary;
        return 0;
      }
    }
  }

  return -E_NOT_FOUND;
}

// Overview:
//  Alloc a new File structure under specified directory. Set *file
//  to point at a free File structure in dir.
// 在目录下创建文件，把文件保存到指针
int dir_alloc_file(struct File *dictionary, struct File **file_pointer) {
  // 目录占据的磁盘块数目
  u_int block_num = dictionary->f_size / BLOCK_SIZE;
  struct File *file;
  void *block;

  int func_info;

  for (int i = 0; i < block_num; i++) {
    // 获取文件第i个磁盘块
    if ((func_info = file_get_block(dictionary, i, &block)) < 0) {
      return func_info;
    }
    // 遍历磁盘块中的文件控制块
    file = block;
    for (int j = 0; j < FILE2BLK; j++) {
      // 遇到空文件
      if (file[j].f_name[0] == '\0') {
        *file_pointer = &file[j];
        return 0;
      }
    }
  }

  // 原目录的磁盘块下没有空余的文件控制块
  dictionary->f_size += BLOCK_SIZE;
  // 为目录增加一个磁盘块
  if ((func_info = file_get_block(dictionary, block_num, &block)) < 0) {
    return func_info;
  }
  file = block;
  *file_pointer = file;

  return 0;
}

// Overview:
//  Skip over slashes.
// 跳过路径中的 '/'
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
// 解析路径，根据路径不断找到目录下的文件，通过文件控制块返回
// 使用指针保存文件和文件所在的目录
// 如果解析到的目录下不包含文件，则将last_element设置为最后找不到的文件名
int walk_path(char *path, struct File **dictionary_pointer, struct File **file_pointer, char *last_element) {
  // 当前正在解析的文件/文件夹名
  char name[MAXNAMELEN] = {0};
  // 解析文件名的临时指针，保存文件名的开始位置
  char *name_begin;
  // 当前的文件
  struct File *file = &(super->s_root);
  // 当前所在的目录
  struct File *dictionary = 0;

  int func_info;

  // 跳过 '/'
  path = skip_slash(path);

  // 初始化返回值
  if (dictionary_pointer) {
    *dictionary_pointer = 0;
  }
  *file_pointer = 0;

  // 解析路径
  while (*path != '\0') {
    // 保存当前所在文件夹为上一次解析的文件
    dictionary = file;
    // 保存文件/文件夹名的起始地址
    name_begin = path;
    while (*path != '/' && *path != '\0') {
      path++;
    }
    // 如果文件/文件夹名过长
    if (path - name_begin >= MAXNAMELEN) {
      return -E_BAD_PATH;
    }
    // 获得解析到的文件/文件夹名
    memcpy(name, name_begin, path - name_begin);
    name[path - name_begin] = '\0';

    // 跳过 '/'
    path = skip_slash(path);
    if (dictionary->f_type != FTYPE_DIR) {
      return -E_NOT_FOUND;
    }

    // 在当前目录下找到文件
    if ((func_info = dir_lookup(dictionary, name, &file)) < 0) {
      // 如果没找到文件 且 解析到路径尾
      if (func_info == -E_NOT_FOUND && *path == '\0') {
        // 如果需要保存最后的文件夹
        if (dictionary_pointer) {
          *dictionary_pointer = dictionary;
        }
        // 保存找不到的文件名
        if (last_element) {
          strcpy(last_element, name);
        }

        *file_pointer = 0;
      }

      return func_info;
    }
  }

  // 保存返回值
  if (dictionary_pointer) {
    *dictionary_pointer = dictionary;
  }
  *file_pointer = file;

  return 0;
}

// Overview:
//  Open "path".
//
// Post-Condition:
//  On success set *pfile to point at the file and return 0.
//  On error return < 0.
// 通过解析path得到文件
int file_open(char *path, struct File **file) {
  return walk_path(path, 0, file, 0);
}

// Overview:
//  Create "path".
//
// Post-Condition:
//  On success set *file to point at the file and return 0.
//  On error return < 0.
// 按照路径创建文件
int file_create(char *path, struct File **file_pointer) {
  char name[MAXNAMELEN];
  struct File *dictionary;
  struct File *file;
  int func_info;

  func_info = walk_path(path, &dictionary, &file, name);
  // 如果文件已经存在
  if (func_info == 0) {
    return -E_FILE_EXISTS;
  }

  // 目录不存在
  if (func_info != -E_NOT_FOUND || dictionary == 0) {
    return func_info;
  }
  // 在目录下创建文件，获得文件控制块
  if (dir_alloc_file(dictionary, &file) < 0) {
    return func_info;
  }
  // 为文件控制块拷贝名字
  strcpy(file->f_name, name);

  *file_pointer = file;
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
// 缩小文件尺寸至new_size
void file_truncate(struct File *file, u_int new_size) {
  // 获取size占用的磁盘块数
  u_int old_nblock_num = ROUND(file->f_size, BLOCK_SIZE) / BLOCK_SIZE;
  u_int new_nblock_num = ROUND(new_size, BLOCK_SIZE) / BLOCK_SIZE;
  if (new_size == 0) {
    new_nblock_num = 0;
  }

  // 如果新的尺寸全部在直接指针区域
  if (new_nblock_num <= NDIRECT) {
    // 将多余部分清空
    for (int block_no = new_nblock_num; block_no < old_nblock_num; block_no++) {
      panic_on(file_clear_block(file, block_no));
    }
    // 直接清空间接指针磁盘块
    if (file->f_indirect) {
      free_block(file->f_indirect);
      file->f_indirect = 0;
    }
  }
  // 在间接指针区域
  else {
    for (int block_no = new_nblock_num; block_no < old_nblock_num; block_no++) {
      // 将对应磁盘块清空
      panic_on(file_clear_block(file, block_no));
    }
  }
  // 设置新大小
  file->f_size = new_size;
}

// Overview:
//  Set file size to newsize.
// 将文件设置为新大小，判断了
int file_set_size(struct File *file, u_int new_size) {
  // 如果是缩小尺寸，清空不用的磁盘块
  if (file->f_size > new_size) {
    file_truncate(file, new_size);
  }
  // 多余情况直接设置
  file->f_size = new_size;
  // 写回文件夹
  if (file->f_dir) {
    file_flush(file->f_dir);
  }

  return 0;
}

// Overview:
//  Flush the contents of file f out to disk.
//  Loop over all the blocks in file.
//  Translate the file block number into a disk block number and then
//  check whether that disk block is dirty. If so, write it out.
// 将文件的内容脏部分写回磁盘
void file_flush(struct File *file) {
  // 获取文件占有的磁盘块数
  u_int block_num = ROUND(file->f_size, BLOCK_SIZE) / BLOCK_SIZE;
  u_int disk_no;

  // 遍历文件的磁盘块
  for (int block_no = 0; block_no < block_num; block_no++) {
    // 获取文件的磁盘块对应的磁盘块号
    if (file_map_block(file, block_no, &disk_no, 0)) {
      continue;
    }
    // 如果文件是脏的，则写回到磁盘
    if (block_is_dirty(disk_no)) {
      write_block(disk_no);
    }
  }
}

// Overview:
//  Sync the entire file system.  A big hammer.
// 同步文件系统
void fs_sync(void) {
  // 遍历文件系统所有磁盘块
  for (int block_no = 0; block_no < super->s_nblocks; block_no++) {
    // 如果有修改，则写回至磁盘
    if (block_is_dirty(block_no)) {
      write_block(block_no);
    }
  }
}

// Overview:
//  Close a file.
// 关闭文件
void file_close(struct File *file) {
  // 写回文件至磁盘
  file_flush(file);
  // 把目录写回磁盘
  if (file->f_dir) {
    file_flush(file->f_dir);
  }
}

// Overview:
//  Remove a file by truncating it and then zeroing the name.
// 通过路径移除文件
int file_remove(char *path) {
  struct File *file;
  int func_info;

  // 通过路径获得文件，保存到指针
  if ((func_info = walk_path(path, 0, &file, 0)) < 0) {
    return func_info;
  }

  // 清楚文件控制块的信息
  // 将文件的大小设置为0
  file_truncate(file, 0);
  // 将文件名清空
  file->f_name[0] = '\0';
  // 将文件有修改的部分写回到磁盘
  file_flush(file);
  // 文件有目录，去除目录信息
  if (file->f_dir) {
    file_flush(file->f_dir);
  }

  return 0;
}
