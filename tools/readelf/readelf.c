#include "elf.h"
#include <stdio.h>

// 32位大小端转换
#define REVERSE_32(n) \
    ((((n)&0xff) << 24) | (((n)&0xff00) << 8) | (((n) >> 8) & 0xff00) | (((n) >> 24) & 0xff))

// 16位大小端转换
#define REVERSE_16(n) \
    ((((n)&0xff) << 8) | (((n) >> 8) & 0xff))

/* Overview:
 *   Check whether specified buffer is valid ELF data.
 *
 * Pre-Condition:
 *   The memory within [binary, binary+size) must be valid to read.
 *
 * Post-Condition:
 *   Returns 0 if 'binary' isn't an ELF, otherwise returns 1.
 */

// 通过文件头的魔数确定是否为ELF文件
// size为文件大小
// binary为申请到的空间，大小为size+1
int is_elf_format(const void *binary, size_t size) {
  // 得到存储文件信息的指针
	Elf32_Ehdr *ehdr = (Elf32_Ehdr *)binary;
	// 通过魔数进行确认
	return size >= sizeof(Elf32_Ehdr) && 
         ehdr->e_ident[EI_MAG0] == ELFMAG0 &&
         ehdr->e_ident[EI_MAG1] == ELFMAG1 && 
         ehdr->e_ident[EI_MAG2] == ELFMAG2 &&
         ehdr->e_ident[EI_MAG3] == ELFMAG3;
}

/* Overview:
 *   Parse the sections from an ELF binary.
 *
 * Pre-Condition:
 *   The memory within [binary, binary+size) must be valid to read.
 *
 * Post-Condition:
 *   Return 0 if success. Otherwise return < 0.
 *   If success, output the address of every section in ELF.
 */

// 默认是小端存储文件
// 大小端转换用到的数据都需要转换：节中存储的信息
int readelf(const void *binary, size_t size) {
  // ehdr是文件头指针
	Elf32_Ehdr *ehdr = (Elf32_Ehdr *)binary;

	// Check whether `binary` is a ELF file.
	if (!is_elf_format(binary, size)) {
		fputs("not an elf file\n", stderr);
		return -1;
	}

	// Get:
  // - the address of the section table
  // - the number of section headers
  // - the size of a section header.
  // 获取的是节头表：记录了该节程序的代码段、数据段等各个段的内容
	const void *sh_table;
	Elf32_Half sh_entry_count;
	Elf32_Half sh_entry_size;
	/* Exercise 1.1: Your code here. (1/2) */
  // 通过偏移量得到节头表
	sh_table = binary + ehdr->e_shoff;
  // 得到节头表表项数
	sh_entry_count = ehdr->e_shnum;
  // 每一节表项的大小
	sh_entry_size = ehdr->e_shentsize;

	// For each section header, output its index and the section address.
	// The index should start from 0.
	for (int i = 0; i < sh_entry_count; i++) {
		const Elf32_Shdr *shdr;
		unsigned int addr;
		/* Exercise 1.1: Your code here. (2/2) */
		shdr = sh_table + i*sh_entry_size;
		addr = shdr->sh_addr;
    // int offest = shdr->sh_offset;
    // int allign = shdr->sh_addralign;

		printf("%d:0x%x\n", i, addr);
	}

	return 0;
}
