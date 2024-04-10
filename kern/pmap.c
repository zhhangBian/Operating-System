#include <bitops.h>
#include <env.h>
#include <malta.h>
#include <mmu.h>
#include <pmap.h>
#include <printk.h>

// 相关转换函数：
// PPN  获取物理地址对应的页表号
// VPN  获取虚拟地址对应的页表号

// page2ppn  获取对应的控制块是第几个页
// page2pa(&page)  获取页控制块控制页面对应的物理地址
// pa2page(pa)  通过物理地址获取对应的页控制块
// page2kva  获取页控制块控制页面对应的在kseg0中的虚拟地址

// PADDR(va)  将 kseg0 中的虚拟地址转化为物理地址
// KADDR(pa)  将物理地址转化为在kseg0中的虚拟地址

// PDX  获取一级页表项 31-22位
// PTX  获取二级页表项 21-12位
// PTE_ADDR 返回页目录对应的二级页表的基地址，低12位抹0

// Pde *pde = pde_base + PDX(virtural_address);
// Pte *pte_pointer = (Pte *)KADDR(PTE_ADDR(*pde)) + PTX(virtural_address);

// void *alloc：分配一定的物理内存。仅在建立虚拟内存系统时会被使用。
// void mips_vm_init：只干了一件事：为二级页表结构分配空间。
// void page_init：初始化页表管理结构，将空闲页表都插入到 page_free_list 中。
// int page_alloc：从空闲页表中取出一页并分配。
// int page_free：释放一页，
// int pgdir_walk：通过给出的页目录基地址，找到虚拟地址 va 对应的页表项并将其地址赋给 ppte。若不存在页表项且 create 为 1 则为其分配一个页表。
// int page_insert：将虚拟地址 va 映射到 pp 对应的物理页面。
// struct Page *page_lookup：寻找虚拟地址 va 映射到的页面。将二级页表项地址赋值给 ppte。返回对应的页控制块。
// void page_decref：减少页面的引用。若减少后引用数为 0，则释放这一页。
// void page_remove：取消 va 映射的页面。
// void tlb_invalidate：使 TLB 中带有 asid 的 va 地址的表项无效。

/* These variables are set by mips_detect_memory(ram_low_size); */
static u_long memsize; /* Maximum physical address */
u_long npage;	       /* Amount of memory(in pages) */

// 存储了当前进程一级页表基地址位于kseg0的虚拟地址。
Pde *cur_pgdir;

// pages是一个保存了所有页控制模块的数组
struct Page *pages;
// 表示可用虚拟内存地址的全局变量
static u_long free_memory_address;
// 维护空闲页链表，在之后初始化
struct Page_list page_free_list; /* Free list of physical pages */

/* Overview:
 *   Use '_memsize' from bootloader to initialize 'memsize' and
 *   calculate the corresponding 'npage' value.
 */
void mips_detect_memory(u_int _memsize) {
  // 使用bootloader传递的参数确认物理内存的大小
  memsize = _memsize;

  // 确认页的数量
  npage = memsize / PAGE_SIZE;

  printk("Memory size: %lu KiB, number of pages: %lu\n", memsize / 1024, npage);
}

/* Lab 2 Key Code "alloc" */
/* Overview:
    Allocate `n` bytes physical memory with alignment `align`, if `set_zero` is set, set_zero the
    allocated memory.
    This allocator is used only while setting up virtual memory system.
   Post-Condition:
    If we're out of memory, should panic, else return this address of memory we have allocated.*/
// 申请 n 个字节大小的空间，按照 align 进行对齐
void *alloc(u_int n, u_int align, int set_zero) {
  // 找到end  . = 0x80400000;   end = . ;
  extern char end[];
  // 指针，本质是一个地址
  u_long alloced_mem;

  /* Initialize `free_memory_address` if this is the first time. The first virtual address that the
   * linker did *not* assign to any kernel code or global variables. */
  if (free_memory_address == 0) {
    free_memory_address = (u_long)end; // end
  }

  /* Step 1: Round up `free_memory_address` up to be aligned properly */
  // 将freemen进行对齐
  free_memory_address = ROUND(free_memory_address, align);

  // 本次申请的内存地址（虚拟地址）
  alloced_mem = free_memory_address;

  // 将可用地址增加此次申请的字节数，便于下一次调用
  free_memory_address = free_memory_address + n;

  // Panic if we're out of memory.（物理地址）
  panic_on(PADDR(free_memory_address) >= memsize);

  // 是否将申请的内存初始化
  if (set_zero) {
    memset((void *)alloced_mem, 0, n);
  }

  /* Step 5: return allocated chunk. */
  return (void *)alloced_mem;
}
/* End of Key Code "alloc" */

/* Overview:
    Set up two-level page table.
   Hint:
    You can get more details about `UPAGES` and `UENVS` in include/mmu.h. */
void mips_vm_init() {
  /* Allocate proper size of physical memory for global array `pages`,
   * for physical memory management. Then, map virtual address `UPAGES` to
   * physical address `pages` allocated before. For consideration of alignment,
   * you should round up the memory size before map. */
  // 申请了 npage 个 (struct Page) 大小的内存。并以 每个页的大小 进行对齐。
  // 同时将申请的内存中内容初始化为 0
  pages = (struct Page *)alloc(npage * sizeof(struct Page), PAGE_SIZE, 1);
  printk("to memory %x for struct Pages.\n", free_memory_address);
  printk("pmap.c:\t mips vm init success\n");
}

/* Overview:
 *   Initialize page structure and memory free list. The 'pages' array has one 'struct Page' entry
 * per physical page. Pages are reference counted, and free pages are kept on a linked list.
 *
 * Hint: Use 'LIST_INSERT_HEAD' to insert free pages to 'page_free_list'.
 */
void page_init(void) {
  /* Step 1: Initialize page_free_list. */
  // 创建一个空闲页组成的链表
  LIST_INIT(&page_free_list);

  /* Step 2: Align `free_memory_address` up to multiple of PAGE_SIZE. */
  // 将当前已使用的空间进行对其
  free_memory_address = ROUND(free_memory_address, PAGE_SIZE);

  /* Step 3: Mark all memory below `free_memory_address` as used (set `pp_ref` to 1) */
  // 先获取free_memory_address地址对应的物理地址（已经使用的空间），再获取对应的页表号
  u_long page_used = PPN(PADDR(free_memory_address));
  // 将已使用的内存对应的页控制块的使用次数设置为1
  // 为什么是1：当前为初始化，是第一次设置页控制块
  for(u_long i=0; i< page_used; i++) {
    pages[i].pp_ref = 1;
  }

  // 很有意思，从高地址往下，kuseg是栈区
  for(u_long i=page_used; i<npage; i++) {
    pages[i].pp_ref = 0;
    // 为什么取地址：将地址转换为一个指针变量是方便的
    LIST_INSERT_HEAD(&page_free_list, &pages[i], pp_link);
  }
}

/* Overview:
 *   Allocate a physical page from free memory, and fill this page with zero.
 *
 * Post-Condition:
 *   If failed to allocate a new page (out of memory, there's no free page), return -E_NO_MEM.
 *   Otherwise, set the address of the allocated 'Page' to *pp, and return 0.
 *
 * Note:
 *   This does NOT increase the reference count 'pp_ref' of the page - the caller must do these if
 *   necessary (either explicitly or via page_insert).
 *
 * Hint: Use LIST_FIRST and LIST_REMOVE defined in include/queue.h.
 */
// 创建相应的页控制块，从空闲页表链表中移除
// 每个物理地址自动对应一个页控制块：实际的分配只能是物理地址
int page_alloc(struct Page **new) {
  /* Step 1: Get a page from free memory. If fails, return the error code.*/
  if(LIST_EMPTY(&page_free_list)) {
    return -E_NO_MEM;
  }

  struct Page *page_alloced;
  page_alloced = LIST_FIRST(&page_free_list);
  LIST_REMOVE(page_alloced, pp_link);

  /* Step 2: Initialize this page with zero. */
  // 获取页控制块对应的虚拟地址，并进行初始化清空
  memset((void *)page2kva(page_alloced), 0, PAGE_SIZE);

  *new = page_alloced;
  return 0;
}

/* Overview:
 *   Release a page 'pp', mark it as free.
 *
 * Pre-Condition:
 *   'pp->pp_ref' is '0'.
 */
void page_free(struct Page *page_pointer) {
  assert(page_pointer->pp_ref == 0);
  /* Just insert it into 'page_free_list'. */
  LIST_INSERT_HEAD(&page_free_list, page_pointer, pp_link);
}

/* Overview:
 *   Given 'pgdir', a pointer to a page directory, 'pgdir_walk' returns a pointer to
 *   the page table entry for virtual address 'va'.
 *
 * Pre-Condition:
 *   'pgdir' is a two-level page table structure.
 *   'ppte' is a valid pointer, i.e., it should NOT be NULL.
 *
 * Post-Condition:
 *   If we're out of memory, return -E_NO_MEM.
 *   Otherwise, we get the page table entry, store
 *   the value of page table entry to *ppte, and return 0, indicating success.
 *
 * Hint:
 *   We use a two-level pointer to store page table entry and return a state code to indicate
 *   whether this function succeeds or not.
 */

// 实现查找对应虚拟地址对应的（二级）页表项
static int pgdir_walk(Pde *pde_base, u_long virtural_address, int if_create, Pte **pte_pointer) {
  struct Page *page_pointer;
  // 页表地址都是实际的物理地址，根据虚拟地址获取相应的便宜量，得到一级页表
  Pde *pde = pde_base + PDX(virtural_address);

  // 判断页目录是否有对应二级页表是否有效
  // 如果无效
  if (!(*pde & PTE_V)) {
    if (if_create) {
      // 在物理地址中寻找一页，分配给这个虚拟地址
      if (page_alloc(&page_pointer) != 0) { return -E_NO_MEM;}
      page_pointer->pp_ref++;

      // 设置页表项  物理地址（页控制块一一对应） + 权限位
      *pde = page2pa(page_pointer) | PTE_C_CACHEABLE | PTE_V;
    } else {
      *pte_pointer = NULL;
      return 0;
    }
  }

  // 获取虚拟地址对应的页表的基地址： 页目录对应的二级页表基地址 -> 虚拟地址
  Pte *pte_base = (Pte *)KADDR(PTE_ADDR(*pde));
  *pte_pointer = pte_base + PTX(virtural_address);

  return 0;
}

/* Overview:
 *   Map the physical page 'pp' at virtual address 'va'. The permission (the low 12 bits) of the
 *   page table entry should be set to 'perm | PTE_C_CACHEABLE | PTE_V'.
 *
 * Post-Condition:
 *   Return 0 on success
 *   Return -E_NO_MEM, if page table couldn't be allocated
 *
 * Hint:
 *   If there is already a page mapped at `va`, call page_remove() to release this mapping.
 *   The `pp_ref` should be incremented if the insertion succeeds.
 */
// 增加页表映射关系
// 将虚拟地址 virtual_address 映射到页控制块 page_pointer 对应的物理页面，并将页表项权限为设置为perm
int page_insert(Pde *pde_base, u_int asid, struct Page *page_pointer,
                u_long virtual_address, u_int perm) {
  Pte *pte;

  // 查找 virtual_address 是否存在原有的页表映射关系
  pgdir_walk(pde_base, virtual_address, 0, &pte);

  // 如果存在原有二级页表映射
  if (pte && (*pte & PTE_V)) {
    // 存在的映射是有效的，通过比较页控制块判断现有映射的页和需要插入的页是否一样
    // 不一样则删除
    if (pa2page(*pte) != page_pointer) {
      page_remove(pde_base, asid, virtual_address);
    } else {
      // 修改页表的地址映射关系和权限，将TLB无效，
      *pte = page2pa(page_pointer) | perm | PTE_C_CACHEABLE | PTE_V;
      tlb_invalidate(asid, virtual_address);
      return 0;
    }
  }

  // 不存在相应的二级页表，自然没有tlb映射，这一步是不必要的
  tlb_invalidate(asid, virtual_address);
  // 创建相应二级页表项
  if (pgdir_walk(pde_base, virtual_address, 1, &pte) != 0) {
    return -E_NO_MEM;
  }
  // 修改二级页表项的内容：  地址  +  权限
  *pte = page2pa(page_pointer) | perm | PTE_C_CACHEABLE | PTE_V;
  // 同时递增页控制块的引用计数
  page_pointer->pp_ref++;

  return 0;
}

/* Lab 2 Key Code "page_lookup" */
/*Overview:
    Look up the Page that virtual address `va` map to.
  Post-Condition:
    Return a pointer to corresponding Page, and store it's page table entry to *ppte.
    If `va` doesn't mapped to any Page, return NULL.*/
// 返回虚拟地址对应的页控制块，同时设置相关的二级页表项
struct Page *page_lookup(Pde *pde_base, u_long virtual_address, Pte **pte_pointer) {
  struct Page *page_pointer;
  Pte *pte;

  // 查找对应的二级页表项，存储在pte指针上返回，如果没找到不创建
  pgdir_walk(pde_base, virtual_address, 0, &pte);

  // 查找是否存在 或 存在但页面不在内存中
  if (pte == NULL || (*pte & PTE_V) == 0) {
    return NULL;
  }

  // 如果二级页表存在，获取相应的页控制块
  page_pointer = pa2page(*pte);
  //如果调用方需要这一页表项的地址（传入了用于传递页表项地址的空间的地址）则将页表项地址传递
  if (pte_pointer) {
    *pte_pointer = pte;
  }

  return page_pointer;
}
/* End of Key Code "page_lookup" */

/* Overview:
 *   Decrease the 'pp_ref' value of Page 'pp'.
 *   When there's no references (mapped virtual address) to this page, release it.
 */
// 将页面引用 -1，如果为0则自动释放
void page_decref(struct Page *pp) {
  assert(pp->pp_ref > 0);

  /* If 'pp_ref' reaches to 0, free this page. */
  if (--(pp->pp_ref) == 0) {
    page_free(pp);
  }
}

/* Lab 2 Key Code "page_remove" */
// Overview:
//   Unmap the physical page at virtual address 'virtual_address'.
// 删除原有的虚拟地址-物理地址映射关系
void page_remove(Pde *pgdir, u_int asid, u_long virtual_address) {
  Pte *pte;

  /* Step 1: Get the page table entry, and check if the page table entry is valid. */
  // 查找虚拟地址对应的 二级页表 和 页控制块
  struct Page *page_pointer = page_lookup(pgdir, virtual_address, &pte);
  if (page_pointer == NULL) {
    return;
  }

  // 删除页表映射：从原有虚拟地址变为0
  *pte = 0;
  /* Step 2: Decrease reference count on 'pp'. */
  page_decref(page_pointer);

  /* Step 3: Flush TLB. */
  // 因为对页表进行了修改，需要调用 tlb_invalidate 确保 TLB 中不保留原有内容。
  tlb_invalidate(asid, virtual_address);
  return;
}
/* End of Key Code "page_remove" */

void physical_memory_manage_check(void) {
  struct Page *pp, *pp0, *pp1, *pp2;
  struct Page_list fl;
  int *temp;

  // should be able to allocate three pages
  pp0 = pp1 = pp2 = 0;
  assert(page_alloc(&pp0) == 0);
  assert(page_alloc(&pp1) == 0);
  assert(page_alloc(&pp2) == 0);

  assert(pp0);
  assert(pp1 && pp1 != pp0);
  assert(pp2 && pp2 != pp1 && pp2 != pp0);

  // temporarily steal the rest of the free pages
  fl = page_free_list;
  // now this page_free list must be empty!!!!
  LIST_INIT(&page_free_list);
  // should be no free memory
  assert(page_alloc(&pp) == -E_NO_MEM);

  temp = (int *)page2kva(pp0);
  // write 1000 to pp0
  *temp = 1000;
  // free pp0
  page_free(pp0);
  printk("The number in address temp is %d\n", *temp);

  // alloc again
  assert(page_alloc(&pp0) == 0);
  assert(pp0);

  // pp0 should not change
  assert(temp == (int *)page2kva(pp0));
  // pp0 should be zero
  assert(*temp == 0);

  page_free_list = fl;
  page_free(pp0);
  page_free(pp1);
  page_free(pp2);
  struct Page_list test_free;
  struct Page *test_pages;
  test_pages = (struct Page *)alloc(10 * sizeof(struct Page), PAGE_SIZE, 1);
  LIST_INIT(&test_free);
  // LIST_FIRST(&test_free) = &test_pages[0];
  int i, j = 0;
  struct Page *p, *q;
  for (i = 9; i >= 0; i--) {
    test_pages[i].pp_ref = i;
    // test_pages[i].pp_link=NULL;
    // printk("0x%x  0x%x\n",&test_pages[i], test_pages[i].pp_link.le_next);
    LIST_INSERT_HEAD(&test_free, &test_pages[i], pp_link);
    // printk("0x%x  0x%x\n",&test_pages[i], test_pages[i].pp_link.le_next);
  }
  p = LIST_FIRST(&test_free);
  int answer1[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  assert(p != NULL);
  while (p != NULL) {
    // printk("%d %d\n",p->pp_ref,answer1[j]);
    assert(p->pp_ref == answer1[j++]);
    // printk("ptr: 0x%x v: %d\n",(p->pp_link).le_next,((p->pp_link).le_next)->pp_ref);
    p = LIST_NEXT(p, pp_link);
  }
  // insert_after test
  int answer2[] = {0, 1, 2, 3, 4, 20, 5, 6, 7, 8, 9};
  q = (struct Page *)alloc(sizeof(struct Page), PAGE_SIZE, 1);
  q->pp_ref = 20;

  // printk("---%d\n",test_pages[4].pp_ref);
  LIST_INSERT_AFTER(&test_pages[4], q, pp_link);
  // printk("---%d\n",LIST_NEXT(&test_pages[4],pp_link)->pp_ref);
  p = LIST_FIRST(&test_free);
  j = 0;
  // printk("into test\n");
  while (p != NULL) {
    //      printk("%d %d\n",p->pp_ref,answer2[j]);
    assert(p->pp_ref == answer2[j++]);
    p = LIST_NEXT(p, pp_link);
  }

  printk("physical_memory_manage_check() succeeded\n");
}

void page_check(void) {
  struct Page *pp, *pp0, *pp1, *pp2;
  struct Page_list fl;

  // should be able to allocate a page for directory
  assert(page_alloc(&pp) == 0);
  Pde *boot_pgdir = (Pde *)page2kva(pp);

  // should be able to allocate three pages
  pp0 = pp1 = pp2 = 0;
  assert(page_alloc(&pp0) == 0);
  assert(page_alloc(&pp1) == 0);
  assert(page_alloc(&pp2) == 0);

  assert(pp0);
  assert(pp1 && pp1 != pp0);
  assert(pp2 && pp2 != pp1 && pp2 != pp0);

  // temporarily steal the rest of the free pages
  fl = page_free_list;
  // now this page_free list must be empty!!!!
  LIST_INIT(&page_free_list);

  // should be no free memory
  assert(page_alloc(&pp) == -E_NO_MEM);

  // there is no free memory, so we can't allocate a page table
  assert(page_insert(boot_pgdir, 0, pp1, 0x0, 0) < 0);

  // free pp0 and try again: pp0 should be used for page table
  page_free(pp0);
  assert(page_insert(boot_pgdir, 0, pp1, 0x0, 0) == 0);
  assert(PTE_FLAGS(boot_pgdir[0]) == (PTE_C_CACHEABLE | PTE_V));
  assert(PTE_ADDR(boot_pgdir[0]) == page2pa(pp0));
  assert(PTE_FLAGS(*(Pte *)page2kva(pp0)) == (PTE_C_CACHEABLE | PTE_V));

  printk("va2pa(boot_pgdir, 0x0) is %x\n", va2pa(boot_pgdir, 0x0));
  printk("page2pa(pp1) is %x\n", page2pa(pp1));
  //  printk("pp1->pp_ref is %d\n",pp1->pp_ref);
  assert(va2pa(boot_pgdir, 0x0) == page2pa(pp1));
  assert(pp1->pp_ref == 1);

  // should be able to map pp2 at PAGE_SIZE because pp0 is already allocated for page table
  assert(page_insert(boot_pgdir, 0, pp2, PAGE_SIZE, 0) == 0);
  assert(va2pa(boot_pgdir, PAGE_SIZE) == page2pa(pp2));
  assert(pp2->pp_ref == 1);

  // should be no free memory
  assert(page_alloc(&pp) == -E_NO_MEM);

  printk("start page_insert\n");
  // should be able to map pp2 at PAGE_SIZE because it's already there
  assert(page_insert(boot_pgdir, 0, pp2, PAGE_SIZE, 0) == 0);
  assert(va2pa(boot_pgdir, PAGE_SIZE) == page2pa(pp2));
  assert(pp2->pp_ref == 1);

  // pp2 should NOT be on the free list
  // could happen in ref counts are handled sloppily in page_insert
  assert(page_alloc(&pp) == -E_NO_MEM);

  // should not be able to map at PDMAP because need free page for page table
  assert(page_insert(boot_pgdir, 0, pp0, PDMAP, 0) < 0);

  // insert pp1 at PAGE_SIZE (replacing pp2)
  assert(page_insert(boot_pgdir, 0, pp1, PAGE_SIZE, 0) == 0);

  // should have pp1 at both 0 and PAGE_SIZE, pp2 nowhere, ...
  assert(va2pa(boot_pgdir, 0x0) == page2pa(pp1));
  assert(va2pa(boot_pgdir, PAGE_SIZE) == page2pa(pp1));
  // ... and ref counts should reflect this
  assert(pp1->pp_ref == 2);
  printk("pp2->pp_ref %d\n", pp2->pp_ref);
  assert(pp2->pp_ref == 0);
  printk("end page_insert\n");

  // pp2 should be returned by page_alloc
  assert(page_alloc(&pp) == 0 && pp == pp2);

  // unmapping pp1 at 0 should keep pp1 at PAGE_SIZE
  page_remove(boot_pgdir, 0, 0x0);
  assert(va2pa(boot_pgdir, 0x0) == ~0);
  assert(va2pa(boot_pgdir, PAGE_SIZE) == page2pa(pp1));
  assert(pp1->pp_ref == 1);
  assert(pp2->pp_ref == 0);

  // unmapping pp1 at PAGE_SIZE should free it
  page_remove(boot_pgdir, 0, PAGE_SIZE);
  assert(va2pa(boot_pgdir, 0x0) == ~0);
  assert(va2pa(boot_pgdir, PAGE_SIZE) == ~0);
  assert(pp1->pp_ref == 0);
  assert(pp2->pp_ref == 0);

  // so it should be returned by page_alloc
  assert(page_alloc(&pp) == 0 && pp == pp1);

  // should be no free memory
  assert(page_alloc(&pp) == -E_NO_MEM);

  // forcibly take pp0 back
  assert(PTE_ADDR(boot_pgdir[0]) == page2pa(pp0));
  boot_pgdir[0] = 0;
  assert(pp0->pp_ref == 1);
  pp0->pp_ref = 0;

  // give free list back
  page_free_list = fl;

  // free the pages we took
  page_free(pp0);
  page_free(pp1);
  page_free(pp2);
  page_free(pa2page(PADDR(boot_pgdir)));

  printk("page_check() succeeded!\n");
}

u_int page_filter(Pde *pgdir, u_long va_lower_limit, u_long va_upper_limit, u_int num) {
	struct Page *page;
	Pte *pte;
	u_int count=0;

	for(u_long va=va_lower_limit;va<va_upper_limit;va+=PAGE_SIZE) {
		pgdir_walk(pgdir, va, 0, &pte);

		if(pte==NULL || (*pte & PTE_V)==0) {
			continue;
		}
		page=pa2page(*pte);
		if(page) {
			if(page->pp_ref>=num)
				count++;
		}
	}

	return count;
}
