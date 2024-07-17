#include <lib.h>

/* Overview:
 *   Get the reference count for the physical page mapped at the given address.
 *
 * Post-Condition:
 *   Return the number of virtual pages mapped to this physical page among all envs.
 *   if the virtual address is valid.
 *   Otherwise return 0.
 */
// 获得虚拟地址是否有效，对应的物理内存块映射次数
int pageref(void *virtual_address) {
	u_int pde, pte;

	// 检查页目录是否有效
  pde = vpd[PDX(virtual_address)];
  // 检查页目录是否有效
	if (!(pde & PTE_V)) {
		return 0;
	}

	// 获取页表
	pte = vpt[VPN(virtual_address)];
  // 检查页表是否有效
	if (!(pte & PTE_V)) {
		return 0;
	}
	// 获取对应物理页面的对应映射次数
	return pages[PPN(pte)].pp_ref;
}
