#include <asm/asm.h>
#include <mmu.h>
#include <trap.h>
#include <kclock.h>

.text
LEAF(env_pop_tf)
.set reorder
.set at
  # 将传入的asid值设置到 EntryHi寄存器中
  # 之后的虚拟内存访问都来自于 asid 所对应的进程
  mtc0    a1, CP0_ENTRYHI
  # 将sp寄存器地址设置为当前进程的trap frame地址
  move    sp, a0
  # 设置相应的时钟
  RESET_KCLOCK
  # 从异常处理中返回，将使用当前进程的trap frame恢复上下文
  j       ret_from_exception
END(env_pop_tf)
