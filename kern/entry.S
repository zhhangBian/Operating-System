#include <asm/asm.h>
#include <stackframe.h>

.section .text.tlb_miss_entry
tlb_miss_entry:
  j       exc_gen_entry

# 异常分发程序，当发生异常时由硬件自动跳转到该处
# 在链接时自动将相应程序装入地址（虚拟地址），以实现当前页表下的异常处理
.section .text.exc_gen_entry
exc_gen_entry:
  # 使用SAVE_ALL 宏将当前上下文保存到内核的异常栈中
  SAVE_ALL
  /*
  * Note: When EXL is set or UM is unset, the processor is in kernel mode.
  * When EXL is set, the value of EPC is not updated when a new exception occurs.
  * To keep the processor in kernel mode and enable exception reentrancy,
  * we unset UM and EXL, and unset IE to globally disable interrupts.
  */
  # 清除Status 寄存器中的UM、EXL、IE位
  # 以保持处理器处于内核态（UM==0）、关闭中断且允许嵌套异常。
  mfc0    t0, CP0_STATUS
  and     t0, t0, ~(STATUS_UM | STATUS_EXL | STATUS_IE)
  mtc0    t0, CP0_STATUS
  # 将Cause寄存器的内容拷贝到t0寄存器中
  mfc0    t0, CP0_CAUSE
  # 取得Cause寄存器中的2~6位，也就是对应的异常码
  andi    t0, 0x7c
  # 以得到的异常码作为索引，在exception_handlers数组中找到对应的中断处理函数
  # 得到处理函数后作为返回值填入$t0，在traps.c中
  lw      t0, exception_handlers(t0)
  # 跳转到对应的中断处理函数中，响应异常
  jr      t0