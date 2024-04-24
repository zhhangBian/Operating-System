#include <env.h>
#include <pmap.h>
#include <printk.h>
#include <trap.h>

extern void handle_int(void);
extern void handle_tlb(void);
extern void handle_sys(void);
extern void handle_mod(void);
extern void handle_reserved(void);
extern void handle_ri(void);

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
	[10] = handle_ri,
};

/* Overview:
 *   The fallback handler when an unknown exception code is encountered.
 *   'genex.S' wraps this function in 'handle_reserved'.
 */
void do_reserved(struct Trapframe *tf) {
  print_tf(tf);
  panic("Unknown ExcCode %2d", (tf->cp0_cause >> 2) & 0x1f);
}

void do_ri (struct Trapframe *tf) {
	u_long va = tf->cp0_epc;

	Pte *pte;
	page_lookup(curenv->env_pgdir, va, &pte);
	u_long pa=PTE_ADDR(*pte) | (va & 0xfff);

	u_long kva = KADDR(pa);

	int *instr_p = (int *)kva;
	int instr = *instr_p;

	int rs = (instr>>21)&0x1f;
	int rt = (instr>>16)&0x1f;
	int rd = (instr>>11)&0x1f;

	int func = (instr>>26)&0x3f;
	int shamt = (instr>>6)&0x1f;
	int low6 = instr&0x3f;

	if(func==0 && shamt==0 && low6==0x3f) {
		int rs_num=tf->regs[rs];
		int rt_num=tf->regs[rt];
		
		int rd_num=0;
		for(int i=0;i<32;i+=8) {
			u_int rs_i = rs_num & (0xff<<i);
			u_int rt_i = rt_num & (0xff<<i);
			if(rs_i < rt_i) {
				rd_num = rd_num | rt_i;
			}
			else {
				rd_num = rd_num | rs_i;
			}
		}

		tf->regs[rd]=rd_num;
	}
	else if(func==0 && shamt==0 && low6==0x3e) {
		int rs_nu=tf->regs[rs];
		int *rs_num = (int *)rs_nu;
		int tmp = *rs_num;
		int rt_num=tf->regs[rt];
		int rd_num=tf->regs[rd];
		if(*rs_num == rt_num) {
			*rs_num = rd_num;
		}
		tf->regs[rd] = tmp;
	} 
	else {
		
	}
	tf->cp0_epc = tf->cp0_epc+4;
}	
