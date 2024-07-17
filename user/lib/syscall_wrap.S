#include <asm/asm.h>

# 调用msyscall后，通过syscall陷入内核态，通过异常分发完成系统调用
# 参数由上层的程序进行填入，这里只需要调用syscall
# 是叶函数，没有局部变量，不需要分配栈帧
LEAF(msyscall)
  # 调用syscall陷入内核态
  # 进入handle_sys函数，见genex.S
  syscall
  # 完成系统调用后返回
  jr    ra
END(msyscall)
