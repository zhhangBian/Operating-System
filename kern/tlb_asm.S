#include <asm/asm.h>

LEAF(tlb_out)
.set noreorder
  mfc0    t0, CP0_ENTRYHI
  # 将key写入EntryHi
  mtc0    a0, CP0_ENTRYHI
  nop
  /* Step 1: Use 'tlbp' to probe TLB entry */
  /* Exercise 2.8: Your code here. (1/2) */
  # 利用tlbp使用key查找对应旧表项
  # 将表项的索引存入Index
  tlbp

  nop
  /* Step 2: Fetch the probe result from CP0.Index */
  mfc0    t1, CP0_INDEX
.set reorder
  bltz    t1, NO_SUCH_ENTRY
.set noreorder
  # 如果TLB中存在索引
  mtc0    zero, CP0_ENTRYHI
  mtc0    zero, CP0_ENTRYLO0
  mtc0    zero, CP0_ENTRYLO1
  nop
  /* Step 3: Use 'tlbwi' to write CP0.EntryHi/Lo into TLB at CP0.Index  */
  /* Exercise 2.8: Your code here. (2/2) */
  # 以Index中的值（即为表项的索引），将EntryHi,Lo01的值（0）写入到索引指定的TLB表项中
  tlbwi

.set reorder

NO_SUCH_ENTRY:
  mtc0    t0, CP0_ENTRYHI
  j       ra
END(tlb_out)



# TLB重填函数： do_tlb_refill
# 设置参数
# 使用 NESTED 定义函数标签，表示非叶函数
NESTED(do_tlb_refill, 24, zero)
  # 取出发生 TLB Miss 的虚拟地址到 $a1
  mfc0    a1, CP0_BADVADDR
  # 取出当前进程的 ASID
  mfc0    a2, CP0_ENTRYHI
  # ASID is stored in the lower 8 bits of CP0_ENTRYHI
  andi    a2, a2, 0xff
.globl do_tlb_refill_call;
do_tlb_refill_call:
  # Allocate stack for arguments(3), return value(2), and return address(1)
  # 为返回地址、待填入TLB的页表项以及函数参数传递预留空间
  addi    sp, sp, -24
  # 将返回地址存储到 (sp+20) - (sp+23)
  sw      ra, 20(sp) /* [sp + 20] - [sp + 23] store the return address */
  addi    a0, sp, 12 /* [sp + 12] - [sp + 19] store the return value */
  # 跳转到对应的C函数：
  # 根据虚拟地址和ASID查找页表，将对应的奇偶页表项写回其第一个参数所指定的地址
  jal     _do_tlb_refill /* (Pte *, u_int, u_int) [sp + 0] - [sp + 11] reserved for 3 args */
  lw      a0, 12(sp) /* Return value 0 - Even page table entry */
  lw      a1, 16(sp) /* Return value 1 - Odd page table entry */
  lw      ra, 20(sp) /* Return address */
  addi    sp, sp, 24 /* Deallocate stack */
  mtc0    a0, CP0_ENTRYLO0 /* Even page table entry */
  mtc0    a1, CP0_ENTRYLO1 /* Odd page table entry */
  nop
  /* Hint: use 'tlbwr' to write CP0.EntryHi/Lo into a random tlb entry. */
  /* Exercise 2.10: Your code here. */
  # 执行tlbwr 将此时的EntryHi 与EntryLo0、EntryLo1 写入到TLB中
  tlbwr

  jr      ra
END(do_tlb_refill)
