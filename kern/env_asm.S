#include <asm/asm.h>
#include <mmu.h>
#include <trap.h>
#include <kclock.h>

.text
LEAF(env_pop_tf)
.set reorder
.set at
  # 将传入的asid值设置到 EntryHi寄存器中
	mtc0    a1, CP0_ENTRYHI
  # 将sp寄存器地址设置为当前进程的trap frame地址
	move    sp, a0
	RESET_KCLOCK
	j       ret_from_exception
END(env_pop_tf)
