#include <elf.h>
#include <pmap.h>

// 解析ELF文件头的部分，如果是ELF文件返回对应节头表指针，否则返回NULL
const Elf32_Ehdr *elf_from(const void *binary, size_t size) {
  const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)binary;
  if (size >= sizeof(Elf32_Ehdr) &&
      ehdr->e_ident[EI_MAG0] == ELFMAG0 &&
      ehdr->e_ident[EI_MAG1] == ELFMAG1 &&
      ehdr->e_ident[EI_MAG2] == ELFMAG2 &&
      ehdr->e_ident[EI_MAG3] == ELFMAG3 &&
      ehdr->e_type == 2) {
    // 是可执行文件类型
    return ehdr;
  }
  return NULL;
}

/* Overview:
 *   load an elf format binary file. Map all section
 * at correct virtual address.
 *
 * Pre-Condition:
 *   `binary` can't be NULL and `size` is the size of binary.
 *
 * Post-Condition:
 *   Return 0 if success. Otherwise return < 0.
 *   If success, the entry point of `binary` will be stored in `start`
 */
// 负责将ELF文件的一个段加载到进程中：修改进程的页表使得能够访问
int elf_load_seg(Elf32_Phdr *segment_pointer, // 相应的段指针
                const void *segment_content,  // 需要加载的数据的虚拟地址
                elf_mapper_t load_page,       // 加载单个页面的回调函数
                void *env_data) {             // 需要加载的进程控制块
  // 段所在的虚拟地址
  u_long virtual_address = segment_pointer->p_vaddr;
  // 文件中段的大小
  size_t segment_size_file = segment_pointer->p_filesz;
  // 内存中段的大小
  size_t segment_size_mem = segment_pointer->p_memsz;
  // 加到到内存中的权限
  u_int permission = PTE_V;
  // 如果段是可写的，那么将页面权限设置为有效且可写
  if (segment_pointer->p_flags & PF_W) {
    permission |= PTE_D;
  }

  int func_info;
  size_t i;

  // 首先需要处理要加载的虚拟地址不与页对齐的情况
  // 将最开头不对齐的部分 “剪切” 下来，先映射到内存的页中
  u_long offset = virtual_address - ROUNDDOWN(virtual_address, PAGE_SIZE);
  // 如果也没有对齐
  if (offset != 0) {
    func_info =
      load_page(
        env_data,         // 需要加载的进程控制块
        virtual_address,  // 需要加载到的目的地虚拟地址
        offset,           // 偏移量，对齐后设置为0
        permission,       // 加载到内存后数据的权限
        segment_content,  // 需要加载的数据的虚拟地址
        MIN(segment_size_file, PAGE_SIZE - offset)  // 需要加载的数据的长度
      );
    if (func_info!= 0) {
      return func_info;
    }
  }

  // 加载该段的所有数据（bin）中的所有内容到内存（va）
  // 实际的加载为调用map_page一页一页加载
  for (i = ((offset!=0) ? MIN(segment_size_file, PAGE_SIZE - offset) : 0);
        i < segment_size_file;
        i += PAGE_SIZE) {
    func_info =
      load_page(
        env_data,             // 需要加载的进程控制块
        virtual_address + i,  // 需要加载到的目的地虚拟地址
        0,                    // 偏移量，对齐后设置为0
        permission,           // 加载到页表中的权限
        segment_content + i,  // 需要加载文件的数据的虚拟地址
        MIN(segment_size_file - i, PAGE_SIZE) // 需要加载的数据的长度
      );
    if (func_info!=0) {
      return func_info;
    }
  }

  // 最后处理段大小大于数据大小的情况
  while (i < segment_size_mem) {
    func_info =
      load_page(
        env_data,
        virtual_address + i,
        0,
        permission,
        NULL, // 没有文件需要加载，填充0即可
        MIN(segment_size_mem - i, PAGE_SIZE)
      );
    if (func_info != 0) {
      return func_info;
    }
    i += PAGE_SIZE;
  }

  return 0;
}
