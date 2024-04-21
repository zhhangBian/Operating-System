#include <env.h>
#include <pmap.h>
#include <printk.h>
#include <trap.h>

extern void handle_int(void);
extern void handle_tlb(void);
extern void handle_sys(void);
extern void handle_mod(void);
extern void handle_reserved(void);

// 通过把相应处理函数的地址填到对应数组项中，我们初始化了如下异常：
// - 0号异常的处理函数为handle_int，表示中断，由时钟中断、控制台中断等中断造成
// - 1号异常的处理函数为handle_mod，表示存储异常，进行存储操作时该页被标记为只读
// - 2号异常的处理函数为handle_tlb，表示TLB load异常
// - 3号异常的处理函数为handle_tlb，表示TLB store异常
// - 8号异常的处理函数为handle_sys，表示系统调用，用户进程通过执行syscall指令陷入内核
void (*exception_handlers[32])(void) = {
    // 使用了GNU C的拓展语法[first ... last]=value来对数组某个区间上的元素赋成同一个值
    [0 ... 31] = handle_reserved,
    [0] = handle_int,
    [2 ... 3] = handle_tlb,
#if !defined(LAB) || LAB >= 4
    [1] = handle_mod,
    [8] = handle_sys,
#endif
};

/* Overview:
 *   The fallback handler when an unknown exception code is encountered.
 *   'genex.S' wraps this function in 'handle_reserved'.
 */
void do_reserved(struct Trapframe *tf) {
  print_tf(tf);
  panic("Unknown ExcCode %2d", (tf->cp0_cause >> 2) & 0x1f);
}
