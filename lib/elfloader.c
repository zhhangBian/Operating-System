#include <elf.h>
#include <pmap.h>

// 解析ELF文件头的部分
const Elf32_Ehdr *elf_from(const void *binary, size_t size) {
  const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)binary;
  if (size >= sizeof(Elf32_Ehdr) && ehdr->e_ident[EI_MAG0] == ELFMAG0 &&
      ehdr->e_ident[EI_MAG1] == ELFMAG1 && ehdr->e_ident[EI_MAG2] == ELFMAG2 &&
      ehdr->e_ident[EI_MAG3] == ELFMAG3 && ehdr->e_type == 2) {
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
// 负责将ELF文件的一个段加载到内存
int elf_load_seg(Elf32_Phdr *segment, const void *bin, elf_mapper_t map_page, void *data) {
  u_long virtual_address = segment->p_vaddr;
  size_t segment_size_file = segment->p_filesz;
  size_t segment_size_mem = segment->p_memsz;
  u_int permission = PTE_V;
  if (segment->p_flags & PF_W) {
    permission |= PTE_D;
  }

  int func_info;
  size_t i;
  u_long offset = virtual_address - ROUNDDOWN(virtual_address, PAGE_SIZE);
  if (offset != 0) {
    if ((func_info = map_page(data, virtual_address, offset, permission, bin,
          MIN(segment_size_file, PAGE_SIZE - offset))) != 0) {
      return func_info;
    }
  }

  /* Step 1: load all content of bin into memory. */
  // 加载该段的所有数据（bin）中的所有内容到内存（va）
  for (i = offset ? MIN(segment_size_file, PAGE_SIZE - offset) : 0;
       i < segment_size_file;
       i += PAGE_SIZE) {
    // map_page为回调函数，负责完成单个页面的加载
    func_info = map_page(data, virtual_address + i, 0, permission, bin + i, MIN(segment_size_file - i, PAGE_SIZE));
    if (func_info!=0) {
      return func_info;
    }
  }

  /* Step 2: alloc pages to reach `sgsize` when `bin_size` < `sgsize`. */
  // 分配了新的页面但没能填满（如.bss 区域），那么余下的部分用0来填充。
  while (i < segment_size_mem) {
    // map_page为回调函数，负责完成单个页面的加载
    func_info = map_page(data, virtual_address + i, 0, permission, NULL, MIN(segment_size_mem - i, PAGE_SIZE));
    if (func_info != 0) {
      return func_info;
    }
    i += PAGE_SIZE;
  }

  return 0;
}
