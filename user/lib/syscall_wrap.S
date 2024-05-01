#include <asm/asm.h>

# 参数由上层的程序进行填入，这里只需要调用syscall
LEAF(msyscall)
	// Just use 'syscall' instruction and return.
	/* Exercise 4.1: Your code here. */
  syscall
  jr    ra

END(msyscall)
