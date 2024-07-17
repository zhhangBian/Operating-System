#include <elf.h>
#include <env.h>
#include <lib.h>
#include <mmu.h>

#define debug 0

// 初始化envid对应进程id的栈空间
// 按一定格式将argc、argv写入UTEMP页
int init_stack(u_int envid, char **argv, u_int *init_sp) {
  int argc, tot;
  char *strings;
  u_int *args;
  int func_info;

  // Count the number of arguments (argc)
  // and the total amount of space needed for strings (tot)
  // 计算argc和所需的参数空间大小
  tot = 0;
  for (argc = 0; argv[argc]; argc++) {
    tot += strlen(argv[argc]) + 1;
  }
  // 如果所需栈空间过大，报错
  if (ROUND(tot, 4) + 4 * (argc + 3) > PAGE_SIZE) {
    return -E_NO_MEM;
  }

  // 确定了字符串和指针数组的地址
  strings = (char *)(UTEMP + PAGE_SIZE) - tot;
  args = (u_int *)(UTEMP + PAGE_SIZE - ROUND(tot, 4) - 4 * (argc + 1));
  // 申请UTEMP所在的页
  if ((func_info = syscall_mem_alloc(0, (void *)UTEMP, PTE_D)) < 0) {
    return func_info;
  }

  // Copy the argument strings into the stack page at 'strings'
  // 复制了所有参数字符串
  char *ctemp, *argv_temp;
  ctemp = strings;
  for (int i = 0; i < argc; i++) {
    argv_temp = argv[i];
    for (int j = 0; j < strlen(argv[i]); j++) {
      *ctemp = *argv_temp;
      ctemp++;
      argv_temp++;
    }
    *ctemp = 0;
    ctemp++;
  }

  // Initialize args[0..argc-1] to be pointers to these strings
  // that will be valid addresses for the child environment
  // (for whom this page will be at USTACKTOP-PAGE_SIZE!).
  // 设置了指针数组的内容
  ctemp = (char *)(USTACKTOP - UTEMP - PAGE_SIZE + (u_int)strings);
  for (int i = 0; i < argc; i++) {
    args[i] = (u_int)ctemp;
    ctemp += strlen(argv[i]) + 1;
  }

  // Set args[argc] to 0 to null-terminate the args array.
  ctemp--;
  args[argc] = (u_int)ctemp;

  // Push two more words onto the child's stack below 'args',
  // containing the argc and argv parameters to be passed
  // to the child's main() function.
  // 设置argc的值和argv数组指针的内容
  u_int *pargv_ptr;
  pargv_ptr = args - 1;
  *pargv_ptr = USTACKTOP - UTEMP - PAGE_SIZE + (u_int)args;
  pargv_ptr--;
  *pargv_ptr = argc;

  // Set *init_sp to the initial stack pointer for the child
  // 设置返回栈帧的初始地址
  *init_sp = USTACKTOP - UTEMP - PAGE_SIZE + (u_int)pargv_ptr;

  // 将UTEMP页映射到用户栈真正应该处于的地址
  if ((func_info = 
        syscall_mem_map(0, (void *)UTEMP, 
                        envid, (void *)(USTACKTOP - PAGE_SIZE), 
                        PTE_D)) < 0) {
    goto error;
  }
  if ((func_info = syscall_mem_unmap(0, (void *)UTEMP)) < 0) {
    goto error;
  }

  return 0;

error:
  syscall_mem_unmap(0, (void *)UTEMP);
  return func_info;
}

// 加载程序段的回调函数
// 由于处于用户态，使用了大量的系统调用完成操作
static int spawn_mapper(void *data, u_long va, size_t offset, u_int perm, const void *src, size_t len) {
  u_int child_id = *(u_int *)data;
  try(syscall_mem_alloc(child_id, (void *)va, perm));
  if (src != NULL) {
    int func_info;
    if ((func_info = syscall_mem_map(child_id, (void *)va, 0, (void *)UTEMP, perm | PTE_D)) != 0) {
      syscall_mem_unmap(child_id, (void *)va);
      return func_info;
    }
    memcpy((void *)(UTEMP + offset), src, len);
    return syscall_mem_unmap(0, (void *)UTEMP);
  }
  return 0;
}

/* Note:
 *   This function involves loading executable code to memory. After the completion of load
 *   procedures, D-cache and I-cache writeback/invalidation MUST be performed to maintain cache
 *   coherence, which MOS has NOT implemented. This may result in unexpected behaviours on real
 *   CPUs! QEMU doesn't simulate caching, allowing the OS to function correctly.
 */
// 根据磁盘文件创建一个进程
int spawn(char *file_path, char **argv) {
  // 打开磁盘路径对应的文件
  int fd;
  if ((fd = open(file_path, O_RDONLY)) < 0) {
    return fd;
  }

  int func_info;
  // 读入文件内容到elf_buffer中
  u_char elf_buffer[512];
  if ((func_info = readn(fd, elf_buffer, sizeof(Elf32_Ehdr))) < 0 ||
      func_info != sizeof(Elf32_Ehdr)) {
    goto err;
  }
  // 将文件头转换为 Elf32_Ehdr结构体的格式
  const Elf32_Ehdr *ehdr = elf_from(elf_buffer, sizeof(Elf32_Ehdr));
  if (!ehdr) {
    func_info = -E_NOT_EXEC;
    goto err;
  }
  // 读取了程序入口信息
  u_long entry_point = ehdr->e_entry;

  // 使用系统调用创建一个子进程
  // 为什么不使用fork：会替换子进程的代码和数据，不会再从此处继续执行
  u_int child_envid;
  child_envid = syscall_exofork();
  if (child_envid < 0) {
    func_info = child_envid;
    goto err;
  }

  // 初始化子进程的占空间
  u_int sp;
  if ((func_info = init_stack(child_envid, argv, &sp)) < 0) {
    goto err1;
  }

  // 历整个ELF头的程序段，将程序段的内容读到内存中
  size_t ph_off;
  ELF_FOREACH_PHDR_OFF (ph_off, ehdr) {
    // 设置文件描述符相应的偏移量并读取文件的内容
    if ((func_info = seek(fd, ph_off)) < 0 || readn(fd, elf_buffer, ehdr->e_phentsize) < 0) {
      goto err1;
    }

    Elf32_Phdr *ph = (Elf32_Phdr *)elf_buffer;
    // 如果是需要加载的程序段
    if (ph->p_type == PT_LOAD) {
      void *bin;
      // 先根据程序段相对于文件的偏移得到其在内存中映射到的地址
      if ((func_info = read_map(fd, ph->p_offset, &bin)) < 0) {
        goto err1;
      }
      // 调用elf_load_seg将程序段加载到适当的位置
      if ((func_info = elf_load_seg(ph, bin, spawn_mapper, &child_envid)) < 0) {
        goto err1;
      }
    }
  }
  // 关闭文件
  close(fd);

  // 设置栈帧
  // 父子进程共享USTACKTOP地址之下的数据，但不共享程序部分
  struct Trapframe tf = envs[ENVX(child_envid)].env_tf;
  tf.cp0_epc = entry_point;
  tf.regs[29] = sp;
  if ((func_info = syscall_set_trapframe(child_envid, &tf)) != 0) {
    goto err2;
  }

  // 设置父子进程共享页面
  for (u_int pde_no = 0; pde_no <= PDX(USTACKTOP); pde_no++) {
    if (!(vpd[pde_no] & PTE_V)) {
      continue;
    }
    for (u_int pte_no = 0; pte_no <= PTX(~0); pte_no++) {
      u_int page_no = (pde_no << 10) + pte_no;
      u_int permission = vpt[page_no] & ((1 << PGSHIFT) - 1);
      if ((permission & PTE_V) && (permission & PTE_LIBRARY)) {
        void *va = (void *)(page_no << PGSHIFT);
        if ((func_info = syscall_mem_map(0, va, child_envid, va, permission)) < 0) {
          debugf("spawn: syscall_mem_map %x %x: %d\n", va, child_envid, func_info);
          goto err2;
        }
      }
    }
  }

  // 设定子进程为运行状态以将其加入进程调度队列，实现子进程的创建
  if ((func_info = syscall_set_env_status(child_envid, ENV_RUNNABLE)) < 0) {
    debugf("spawn: syscall_set_env_status %x: %d\n", child_envid, func_info);
    goto err2;
  }
  return child_envid;

// 异常处理程序
// 销毁创建的子进程
err2:
  syscall_env_destroy(child_envid);
  return func_info;
err1:
  syscall_env_destroy(child_envid);
// 关闭打开的文件
err:
  close(fd);
  return func_info;
}

// 将磁盘中的文件加载到内存，并以此创建一个新进程
int spawnl(char *file_path, char *args, ...) {
  // 由于mips的传参机制，可以直接这样传参
  return spawn(file_path, &args);
}
