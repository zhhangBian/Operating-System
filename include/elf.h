/* This file defines standard ELF types, structures, and macros.
   Copyright (C) 1995, 1996, 1997, 1998, 1999 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ian Lance Taylor <ian@cygnus.com>.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef _ELF_H
#define _ELF_H

/* ELF defination file from GNU C Library. We simplefied this
 * file for our lab, removing definations about ELF64, structs and
 * enums which we don't care.
 */

#include <types.h>

/* Type for a 16-bit quantity.  */
typedef uint16_t Elf32_Half;

/* Types for signed and unsigned 32-bit quantities.  */
typedef uint32_t Elf32_Word;
typedef int32_t Elf32_Sword;

/* Types for signed and unsigned 64-bit quantities.  */
typedef uint64_t Elf32_Xword;
typedef int64_t Elf32_Sxword;

/* Type of addresses.  */
typedef uint32_t Elf32_Addr;

/* Type of file offsets.  */
typedef uint32_t Elf32_Off;

/* Type for section indices, which are 16-bit quantities.  */
typedef uint16_t Elf32_Section;

/* Type of symbol indices.  */
typedef uint32_t Elf32_Symndx;

/* The ELF file header.  This appears at the start of every ELF file.  */

#define EI_NIDENT (16)

// 节头表类型
typedef struct {
  // 相关的魔数以及其他信息
  unsigned char e_ident[EI_NIDENT];
  // 文件类型
  Elf32_Half e_type;
  // 机器架构
  Elf32_Half e_machine;
  // 文件版本
  Elf32_Word e_version;
  // 入口点的虚拟地址
  // Entry point virtual address
  Elf32_Addr e_entry;
  // 程序头表所在处与此文件头的偏移量
  // Program header table file offset
  Elf32_Off e_phoff;
  // 节头表所在处与此文件头的偏移
  // Section header table file offset
  Elf32_Off e_shoff;
  // 针对处理器的标记
  // Processor-specific flags
  Elf32_Word e_flags;
  // ELF 文件头的大小（单位为字节）
  // ELF header size in bytes
  Elf32_Half e_ehsize;
  // 程序头表表项大小
  // Program header table entry size
  Elf32_Half e_phentsize;
  // 程序头表表项数
  // Program header table entry count
  Elf32_Half e_phnum;
  // 节头表表项大小
  // Section header table entry size
  Elf32_Half e_shentsize;
  // 节头表表项数
  // Section header table entry count
  Elf32_Half e_shnum;
  // 节头字符串编号
  // Section header string table index
  Elf32_Half e_shstrndx;
} Elf32_Ehdr;

/* Fields in the e_ident array.  The EI_* macros are indices into the
   array.  The macros under each EI_* macro are the values the byte
   may have.  */

#define EI_MAG0 0    /* File identification byte 0 index */
#define ELFMAG0 0x7f /* Magic number byte 0 */

#define EI_MAG1 1   /* File identification byte 1 index */
#define ELFMAG1 'E' /* Magic number byte 1 */

#define EI_MAG2 2   /* File identification byte 2 index */
#define ELFMAG2 'L' /* Magic number byte 2 */

#define EI_MAG3 3   /* File identification byte 3 index */
#define ELFMAG3 'F' /* Magic number byte 3 */

/* Program segment header.  */

// 段头表类型
typedef struct {
  // 段类型
  Elf32_Word p_type;
  // 段在文件中的偏移量
  Elf32_Off p_offset;
  // 段的虚拟地址
  Elf32_Addr p_vaddr;
  // 段的物理地址
  Elf32_Addr p_paddr;
  // 文件中段的大小
  Elf32_Word p_filesz;
  // 内存中段的大小
  Elf32_Word p_memsz;
  // 段标志
  Elf32_Word p_flags;
  // 段对齐
  Elf32_Word p_align;
} Elf32_Phdr;

/* Legal values for p_type (segment type).  */

#define PT_NULL 0	     /* Program header table entry unused */
#define PT_LOAD 1	     /* Loadable program segment */
#define PT_DYNAMIC 2	     /* Dynamic linking information */
#define PT_INTERP 3	     /* Program interpreter */
#define PT_NOTE 4	     /* Auxiliary information */
#define PT_SHLIB 5	     /* Reserved */
#define PT_PHDR 6	     /* Entry for header table itself */
#define PT_NUM 7	     /* Number of defined types.  */
#define PT_LOOS 0x60000000   /* Start of OS-specific */
#define PT_HIOS 0x6fffffff   /* End of OS-specific */
#define PT_LOPROC 0x70000000 /* Start of processor-specific */
#define PT_HIPROC 0x7fffffff /* End of processor-specific */

/* Legal values for p_flags (segment flags).  */

#define PF_X (1 << 0)	       /* Segment is executable */
#define PF_W (1 << 1)	       /* Segment is writable */
#define PF_R (1 << 2)	       /* Segment is readable */
#define PF_MASKPROC 0xf0000000 /* Processor-specific */

/* Utils provided by our ELF loader. */
// 回调函数
typedef int (*elf_mapper_t)(void *data, u_long va, size_t offset, u_int perm, const void *src, size_t len);

const Elf32_Ehdr *elf_from(const void *binary, size_t size);

int elf_load_seg(Elf32_Phdr *ph, const void *bin, elf_mapper_t map_page, void *data);

// 遍历所有程序头表
#define ELF_FOREACH_PHDR_OFF(ph_off, ehdr) \
  (ph_off) = (ehdr)->e_phoff; \
  for (int _ph_idx = 0; _ph_idx < (ehdr)->e_phnum; ++_ph_idx, (ph_off) += (ehdr)->e_phentsize)

#endif /* elf.h */
