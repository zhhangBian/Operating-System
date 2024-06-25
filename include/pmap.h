#ifndef _PMAP_H_
#define _PMAP_H_

#include <mmu.h>
#include <printk.h>
#include <queue.h>
#include <types.h>

extern Pde *cur_pgdir;

// 创建一个名为Page_list，元素类型为Page的链表
// struct Page_list { struct Page *lh_first; }
LIST_HEAD(Page_list, Page);
typedef LIST_ENTRY(Page) Page_LIST_entry_t;

// 代表了对虚页的控制变量
struct Page {
  // 内部包含一个结构体，管理指针：包括前向和后向指针
  Page_LIST_entry_t pp_link;

  // Ref is the count of pointers (usually in page table entries)
  // to this page.  This only holds for pages allocated using
  // page_alloc.  Pages allocated at boot time using pmap.c's "alloc"
  // do not have valid reference count fields.

  // 有多少个物理页对应该虚拟页
  u_short pp_ref;
};

extern struct Page *pages;
extern struct Page_list page_free_list;

// 通过指针减法，得到对应的控制块是第几个页
static inline u_long page2ppn(struct Page *page_pointer) {
  return page_pointer - pages;
}

// page to physical address
// 获取页控制块控制页面对应的物理地址
static inline u_long page2pa(struct Page *page_pointer) {
  return page2ppn(page_pointer) << PGSHIFT;
}

// 通过物理地址获取对应的页控制块
static inline struct Page *pa2page(u_long pa) {
  // 物理地址对应的页表号
  if (PPN(pa) >= npage) {
    panic("pa2page called with invalid pa: %x", pa);
  }
  return &pages[PPN(pa)];
}

// 获取页控制块控制页面对应的在kseg0中的虚拟地址
static inline u_long page2kva(struct Page *page_pointer) {
  return KADDR(page2pa(page_pointer));
}

static inline u_long va2pa(Pde *pgdir, u_long va) {
  Pte *p;

  pgdir = &pgdir[PDX(va)];
  if (!(*pgdir & PTE_V)) {
    return ~0;
  }
  p = (Pte *)KADDR(PTE_ADDR(*pgdir));
  if (!(p[PTX(va)] & PTE_V)) {
    return ~0;
  }
  return PTE_ADDR(p[PTX(va)]);
}

void mips_detect_memory(u_int _memsize);
void mips_vm_init(void);
void mips_init(u_int argc, char **argv, char **penv, u_int ram_low_size);
void page_init(void);
void *alloc(u_int n, u_int align, int clear);

int page_alloc(struct Page **pp);
void page_free(struct Page *pp);
void page_decref(struct Page *pp);
int page_insert(Pde *pgdir, u_int asid, struct Page *pp, u_long va, u_int perm);
struct Page *page_lookup(Pde *pgdir, u_long va, Pte **ppte);
void page_remove(Pde *pgdir, u_int asid, u_long va);

extern struct Page *pages;

void physical_memory_manage_check(void);
void page_check(void);

#endif /* _PMAP_H_ */
