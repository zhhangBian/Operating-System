#include <asm/asm.h>

# 调用msyscall后，通过syscall陷入内核态，通过异常分发完成系统调用
# 参数由上层的程序进行填入，这里只需要调用syscall
# 是叶函数，没有局部变量，不需要分配栈帧
LEAF(msyscall)
 addiu   sp, sp, -24
sw      a0, 4(sp)
sw      a1, 8(sp)
sw      a2, 12(sp)
sw      a3, 16(sp)
sw      ra, 20(sp)
jal     strace_send
lw      a0, 4(sp)
lw      a1, 8(sp)
lw      a2, 12(sp)
lw      a3, 16(sp)
lw      ra, 20(sp)
addiu   sp, sp, 24

 # 调用syscall陷入内核态
  syscall
  # 完成系统调用后返回
  jr    ra
END(msyscall)
