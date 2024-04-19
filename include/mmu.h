#ifndef _MMU_H_
#define _MMU_H_

/*
 * Part 1.  Page table/directory defines.
 */
// 支持的ASID总数
#define NASID 256
// PAGE_SIZE 为 页 对应的字节大小
#define PAGE_SIZE 4096
#define PTMAP PAGE_SIZE
#define PDMAP (4 * 1024 * 1024) // bytes mapped by a page directory entry
#define PGSHIFT 12
#define PDSHIFT 22 // log2(PDMAP)

// 获取一级页表项 31-22位
#define PDX(virtual_address) ((((u_long)(virtual_address)) >> PDSHIFT) & 0x03FF)
// 获取二级页表项 21-12位
#define PTX(virtual_address) ((((u_long)(virtual_address)) >> PGSHIFT) & 0x03FF)

// 返回页目录对应的二级页表的基地址，低12位抹0
#define PTE_ADDR(pte) (((u_long)(pte)) & ~0xFFF)
// 返回页内偏移量
#define PTE_FLAGS(pte) (((u_long)(pte)) & 0xFFF)

// Page number field of an address
// 位运算的背后原因是 页号*页的大小 得到基地址
// 获取物理地址对应的页表号
#define PPN(pa) (((u_long)(pa)) >> PGSHIFT)
// 获取虚拟地址对应的页表号
#define VPN(va) (((u_long)(va)) >> PGSHIFT)

// Page Table/Directory Entry flags

#define PTE_HARDFLAG_SHIFT 6

// TLB EntryLo and Memory Page Table Entry Bit Structure Diagram.
// entrylo.value == pte.value >> 6
/*
 * +----------------------------------------------------------------+
 * |                     TLB EntryLo Structure                      |
 * +-----------+---------------------------------------+------------+
 * |3 3 2 2 2 2|2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0|0 0 0 0 0 0 |
 * |1 0 9 8 7 6|5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6|5 4 3 2 1 0 |
 * +-----------+---------------------------------------+------------+------------+
 * | Reserved  |         Physical Frame Number         |  Hardware  |  Software  |
 * |Zero Filled|                20 bits                |    Flag    |    Flag    |
 * +-----------+---------------------------------------+------------+------------+
 *             |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1|1 1 0 0 0 0 |0 0 0 0 0 0 |
 *             |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2|1 0 9 8 7 6 |5 4 3 2 1 0 |
 *             |                                       |    C D V G |            |
 *             +---------------------------------------+------------+------------+
 *             |                Memory Page Table Entry Structure                |
 *             +-----------------------------------------------------------------+
 */

// 每个权限对应的位不一样，因此可以使用 | 操作来表示不同的权限

// Global bit. When the G bit in a TLB entry is set, that TLB entry will match solely on the VPN
// field, regardless of whether the TLB entry’s ASID field matches the value in EntryHi.
// 全局位，若某页表项的全局位为1，则TLB仅通过虚页号匹配表项，而不匹配ASID，将用于映射pages和envs到用户空间。
#define PTE_G (0x0001 << PTE_HARDFLAG_SHIFT)

// Valid bit. If 0 any address matching this entry will cause a tlb miss exception (TLBL/TLBS).
// 有效位，若某页表项的有效位为1，则该页表项有效，其中高20位就是对应的物理页号。
// 如果为0则任何地址都会引发缺页中断
#define PTE_V (0x0002 << PTE_HARDFLAG_SHIFT)

// Dirty bit, but really a write-enable bit. 1 to allow writes, 0 and any store using this
// translation will cause a tlb mod exception (TLB Mod).
// 可写位，若某页表项的可写位为1，则允许经由该页表项对物理页进行写操作。
#define PTE_D (0x0004 << PTE_HARDFLAG_SHIFT)

// Cache Coherency Attributes bit.
// PTE_C_CACHEABLE 用于配置对应页面的访问属性为可缓存，一般对于所有物理内存页面，都将其配置为可缓存，以允许 CPU 使用 Cache 缓存加速对这些页面的访存请求。
#define PTE_C_CACHEABLE (0x0018 << PTE_HARDFLAG_SHIFT)
#define PTE_C_UNCACHEABLE (0x0010 << PTE_HARDFLAG_SHIFT)

// Copy On Write. Reserved for software, used by fork.
// 写时复制位，用于实现fork 的写时复制机制。
#define PTE_COW 0x0001

// Shared memmory. Reserved for software, used by fork.
// 共享页面位，用于实现管道机制。
#define PTE_LIBRARY 0x0002

// Memory segments (32-bit kernel mode addresses)
#define KUSEG 0x00000000U
#define KSEG0 0x80000000U
#define KSEG1 0xA0000000U
#define KSEG2 0xC0000000U

/*
 * Part 2.  Our conventions.
 */

/*
 o     4G ----------->  +----------------------------+------------0x100000000
 o                      |       ...                  |  kseg2
 o      KSEG2    -----> +----------------------------+------------0xc000 0000
 o                      |          Devices           |  kseg1
 o      KSEG1    -----> +----------------------------+------------0xa000 0000
 o                      |      Invalid Memory        |   /|\
 o                      +----------------------------+----|-------Physical Memory Max
 o                      |       ...                  |  kseg0
 o      KSTACKTOP-----> +----------------------------+----|-------0x8040 0000-------end
 o                      |       Kernel Stack         |    | KSTKSIZE            /|\
 o                      +----------------------------+----|------                |
 o                      |       Kernel Text          |    |                    PDMAP
 o      KERNBASE -----> +----------------------------+----|-------0x8002 0000    |
 o                      |      Exception Entry       | \|/            \|/
 o      ULIM     -----> +----------------------------+------------0x8000 0000-------
 o                      |         User VPT           |     PDMAP                /|\
 o      UVPT     -----> +----------------------------+------------0x7fc0 0000    |
 o                      |           pages            |     PDMAP                 |
 o      UPAGES   -----> +----------------------------+------------0x7f80 0000    |
 o                      |           envs             |     PDMAP                 |
 o  UTOP,UENVS   -----> +----------------------------+------------0x7f40 0000    |
 o  UXSTACKTOP -/       |     user exception stack   |     PTMAP                 |
 o                      +----------------------------+------------0x7f3f f000    |
 o                      |                            |     PTMAP                 |
 o      USTACKTOP ----> +----------------------------+------------0x7f3f e000    |
 o                      |     normal user stack      |     PTMAP                 |
 o                      +----------------------------+------------0x7f3f d000    |
 a                      |                            |                           |
 a                      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                           |
 a                      .                            .                           |
 a                      .                            .                         kuseg
 a                      .                            .                           |
 a                      |~~~~~~~~~~~~~~~~~~~~~~~~~~~~|                           |
 a                      |                            |                           |
 o       UTEXT   -----> +----------------------------+------------0x0040 0000    |
 o                      |      reserved for COW      |     PTMAP                 |
 o       UCOW    -----> +----------------------------+------------0x003f f000    |
 o                      |   reversed for temporary   |     PTMAP                 |
 o       UTEMP   -----> +----------------------------+------------0x003f e000    |
 o                      |       invalid memory       |                  \|/
 a     0 ------------>  +----------------------------+ ----------------------------
 o
*/

#define KERNBASE 0x80020000

#define KSTACKTOP (ULIM + PDMAP)
#define ULIM 0x80000000

#define UVPT (ULIM - PDMAP)
// 用户空间的Pages数组对应的虚拟地址起始处
#define UPAGES (UVPT - PDMAP)
// 用户空间的Envs数组对应的虚拟地址起始处
#define UENVS (UPAGES - PDMAP)

#define UTOP UENVS
#define UXSTACKTOP UTOP

#define USTACKTOP (UTOP - 2 * PTMAP)
#define UTEXT PDMAP
#define UCOW (UTEXT - PTMAP)
#define UTEMP (UCOW - PTMAP)

#ifndef __ASSEMBLER__

/*
 * Part 3.  Our helper functions.
 */
#include <error.h>
#include <string.h>
#include <types.h>

extern u_long npage;

// 一级页表和二级页表长度均为32位，可以用 u_long 类型来表示，结构均为  20物理地址 + 12权限位
// 一级页表项类型  page dictionary  页目录  31-22位
typedef u_long Pde;
// 二级页表项类型  page table  页表  21-12位
typedef u_long Pte;

// a - ULIM 等价于最高三位抹零
// 将 kseg0 中的虚拟地址转化为物理地址
#define PADDR(kseg0_virtual_address)\
  ({\
    u_long _a = (u_long)(kseg0_virtual_address);\
    if (_a < ULIM)\
      panic("PADDR called with invalid kva %08lx", _a);\
    _a - ULIM;\
  })

// translates from physical address to kernel virtual address
// 将物理地址转化为在kseg0中的虚拟地址
#define KADDR(kseg0_physical_address)\
  ({\
    u_long _ppn = PPN(kseg0_physical_address);\
    if (_ppn >= npage) {\
      panic("KADDR called with invalid pa %08lx", (u_long)kseg0_physical_address);\
    }\
    (kseg0_physical_address) + ULIM;\
  })

#define assert(x)\
  do {\
    if (!(x)) {\
      panic("assertion failed: %s", #x);\
    }\
  } while (0)

#define TRUP(_p) \
  ({\
    typeof((_p)) __m_p = (_p);\
    (u_int) __m_p > ULIM ? (typeof(_p))ULIM : __m_p;\
  })

extern void tlb_out(u_int entryhi);
void tlb_invalidate(u_int asid, u_long va);
#endif //!__ASSEMBLER__
#endif // !_MMU_H_
