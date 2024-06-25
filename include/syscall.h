#ifndef SYSCALL_H
#define SYSCALL_H

#ifndef __ASSEMBLER__

// 系统调用号，用于代表相应的系统调用
// 内核区分不同系统调用的唯一依据
enum {
	SYS_putchar,
	SYS_print_cons,
	SYS_getenvid,
	SYS_yield,
	SYS_env_destroy,
	SYS_set_tlb_mod_entry,
	SYS_mem_alloc,
	SYS_mem_map,
	SYS_mem_unmap,
	SYS_exofork,
	SYS_set_env_status,
	SYS_set_trapframe,
	SYS_panic,
	SYS_ipc_try_send,
	SYS_ipc_recv,
	SYS_cgetc,
	SYS_write_dev,
	SYS_read_dev,
  // sigaction
  SYS_SIG_KILL,
  SYS_SIGACTION,
  SYS_SIG_SHIELD,
  SYS_SIG_PENDING,
  SYS_SIG_ENTRY,
  SYS_SIG_FINISH,
  // sigaction end
	MAX_SYSNO,
};

#endif

#endif
