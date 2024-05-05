#include <asm/asm.h>
#include <stackframe.h>

.macro BUILD_HANDLER exception handler
NESTED(handle_\exception, TF_SIZE + 8, zero)
  move    a0, sp
  addiu   sp, sp, -8
  jal     \handler
  addiu   sp, sp, 8
  j       ret_from_exception
END(handle_\exception)
.endm

.text
# 恢复现场
FEXPORT(ret_from_exception)
  RESTORE_ALL
  eret

# 处理中断的程序
# 通过.text.exc_gen_entry代码段的分发，最终调用handle_int函数进行处理
NESTED(handle_int, TF_SIZE, zero)
  # 保存相应的中断信息
  mfc0    t0, CP0_CAUSE
  mfc0    t2, CP0_STATUS
  and     t0, t2
  andi    t1, t0, STATUS_IM7
  # 根据Cause寄存器的值判断是否是Timer对应的7号中断位引发的时钟中断
  bnez    t1, timer_irq
timer_irq:
  # 设置参数：不为强制切换
  li      a0, 0
  # 跳转到对应的调度函数，进行进程调度
  j       schedule
END(handle_int)

BUILD_HANDLER tlb do_tlb_refill

#if !defined(LAB) || LAB >= 4
# 处理页写入异常的内核函数
BUILD_HANDLER mod do_tlb_mod
# 经过异常分发后，通过handle_sys，完成系统调用
# 参数为栈帧
BUILD_HANDLER sys do_syscall
#endif

BUILD_HANDLER reserved do_reserved
