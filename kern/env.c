#include <asm/cp0regdef.h>
#include <elf.h>
#include <env.h>
#include <mmu.h>
#include <pmap.h>
#include <printk.h>
#include <sched.h>

// 所有的进程控制块组成的数组
// 将 envs 数组按照 PAGE_SIZE 字节对齐
struct Env envs[NENV] __attribute__((aligned(PAGE_SIZE)));

// 当前的进程
struct Env *curenv = NULL;	      // the current env

// 处于空闲状态的进程队列
static struct Env_list env_free_list;

// 处于调度态（执行或就绪RUNNABLE）的进程队列
struct Env_sched_list env_sched_list;

// 模板页目录
static Pde *base_pgdir;

// 管理ASID的位图
static uint32_t asid_bitmap[NASID / 32] = {0};
// 创建一个asid
// asid使用位图法进行管理，是有限的
static int asid_alloc(u_int *asid) {
  for (u_int i = 0; i < NASID; ++i) {
    int index = i >> 5;
    int inner = i & 31;
    if ((asid_bitmap[index] & (1 << inner)) == 0) {
      asid_bitmap[index] |= 1 << inner;
      *asid = i;
      return 0;
    }
  }
  return -E_NO_FREE_ENV;
}

/* Overview:
 *  Free an ASID.
 *
 * Pre-Condition:
 *  The ASID is allocated by 'asid_alloc'.
 *
 * Post-Condition:
 *  The ASID is freed and may be allocated again later.
 */
static void asid_free(u_int i) {
  int index = i >> 5;
  int inner = i & 31;
  asid_bitmap[index] &= ~(1 << inner);
}

/* Overview:
 *   Map [va, va+size) of virtual address space to physical [pa, pa+size) in the 'pgdir'. Use
 *   permission bits 'perm | PTE_V' for the entries.
 *
 * Pre-Condition:
 *   'pa', 'va' and 'size' are aligned to 'PAGE_SIZE'.
 */
// 广义的 page_insert。
// 在页目录 pgdir 中，将虚拟地址空间 [va, va+size) 映射到到物理地址空间 [pa, pa+size)，并赋予 perm 权限。
static void map_segment(Pde *pde_base, u_int asid,
                        u_long physical_address, u_long virtual_address,
                        u_int size, u_int permission) {
  assert(physical_address % PAGE_SIZE == 0);
  assert(virtual_address % PAGE_SIZE == 0);
  assert(size % PAGE_SIZE == 0);

  // 挨个将va内的地址映射到物理地址
  for (int i = 0; i < size; i += PAGE_SIZE) {
    // 将虚拟地址为va的页面映射到pa对应的页面控制块，并设置相关的权限
    page_insert(pde_base, asid, pa2page(physical_address + i), virtual_address + i, permission | PTE_V);
  }
}

/* Overview:
 *  This function is to make a unique ID for every env
 *
 * Pre-Condition:
 *  e should be valid
 *
 * Post-Condition:
 *  return e's envid on success
 */
// 获取env对应的id，具有唯一性
u_int mkenvid(struct Env *e) {
  static u_int i = 0;
  return ((++i) << (1 + LOG2NENV)) | (e - envs);
}

/* Overview:
 *   Convert an existing 'envid' to an 'struct Env *'.
 *   If 'envid' is 0, set '*penv = curenv', otherwise set '*penv = &envs[ENVX(envid)]'.
 *   In addition, if 'checkperm' is non-zero, the requested env must be either 'curenv' or its
 *   immediate child.
 *
 * Pre-Condition:
 *   'penv' points to a valid 'struct Env *'.
 *
 * Post-Condition:
 *   return 0 on success, and set '*penv' to the env.
 *   return -E_BAD_ENV on error (invalid 'envid' or 'checkperm' violated).
 */
int envid2env(u_int envid, struct Env **penv, int checkperm) {
  struct Env *e;

  /* Step 1: Assign value to 'e' using 'envid'. */
  /* Hint:
   *   If envid is zero, set 'penv' to 'curenv' and return 0.
   *   You may want to use 'ENVX'.
   */
  /* Exercise 4.3: Your code here. (1/2) */

  if (e->env_status == ENV_FREE || e->env_id != envid) {
    return -E_BAD_ENV;
  }

  /* Step 2: Check when 'checkperm' is non-zero. */
  /* Hints:
   *   Check whether the calling env has sufficient permissions to manipulate the
   *   specified env, i.e. 'e' is either 'curenv' or its immediate child.
   *   If violated, return '-E_BAD_ENV'.
   */
  /* Exercise 4.3: Your code here. (2/2) */

  /* Step 3: Assign 'e' to '*penv'. */
  *penv = e;
  return 0;
}

/* Overview:
 *   Mark all environments in 'envs' as free and insert them into the 'env_free_list'.
 *   Insert in reverse order, so that the first call to 'env_alloc' returns 'envs[0]'.
 *
 * Hints:
 *   You may use these macro definitions below: 'LIST_INIT', 'TAILQ_INIT', 'LIST_INSERT_HEAD'
 */
void env_init(void) {
  int i;
  // 初始化空闲进程列表  和  调度进程列表
  LIST_INIT(&env_free_list);
  TAILQ_INIT(&env_sched_list);

  for (i = NENV - 1; i >= 0; i--) {
    envs[i].env_status = ENV_FREE;
    LIST_INSERT_HEAD(&env_free_list, envs + i, env_link);
  }

  /*
   * Here we first map them into the *template* page directory 'base_pgdir'.
   * Later in 'env_setup_vm', we will copy them into each 'env_pgdir'.
   */
  // 创建模板页目录，将pages和envs映射到 UPAGES 和 UENVS 的空间中，使得每个进程的对应虚拟空间都为pages和envs
  // 设置相关权限为PTE_G，使得只读
  struct Page *p;
  panic_on(page_alloc(&p));
  p->pp_ref++;

  base_pgdir = (Pde *)page2kva(p);
  
  map_segment(base_pgdir, 0, 
              PADDR(pages), UPAGES, // Pages的物理地址和虚拟地址
              ROUND(npage * sizeof(struct Page), PAGE_SIZE), // 映射的地址范围
              PTE_G);
  map_segment(base_pgdir, 0, 
              PADDR(envs), UENVS, // Envs的物理地址和虚拟地址
              ROUND(NENV * sizeof(struct Env), PAGE_SIZE), // 映射的地址范围
              PTE_G);
}

/* Overview:
 *   Initialize the user address space for 'e'.
 */
// 初始化新进程的虚拟地址空间
static int env_setup_vm(struct Env *env) {
  /* Step 1:
   *   Allocate a page for the page directory with 'page_alloc'.
   *   Increase its 'pp_ref' and assign its kernel address to 'env->env_pgdir'.
   *
   * Hint:
   *   You can get the kernel address of a specified physical page using 'page2kva'.
   */
  // 为进程创建页表
  struct Page *page_for_pde;
  try(page_alloc(&page_for_pde));
  page_for_pde->pp_ref++;
  env->env_pgdir=(Pde*)page2kva(page_for_pde);

  /* Step 2: Copy the template page directory 'base_pgdir' to 'e->env_pgdir'. */
  /* Hint:
   *   As a result, the address space of all envs is identical in [UTOP, UVPT).
   *   See include/mmu.h for layout.
   */
  // 将模板页目录中UTOP到UVPT的虚拟地址空间对应的页表项复制到该新页中
  memcpy(env->env_pgdir + PDX(UTOP), 
         base_pgdir + PDX(UTOP), 
         sizeof(Pde) * (PDX(UVPT) - PDX(UTOP)));

  /* Step 3: Map its own page table at 'UVPT' with readonly permission.
   * As a result, user programs can read its page table through 'UVPT' */
  // 设置相关的页表自映射
  // 将UVPT虚拟地址映射到页目录本身的物理地址，并设置只读权限
  env->env_pgdir[PDX(UVPT)] = PADDR(env->env_pgdir) | PTE_V;
  return 0;
}

/* Overview:
 *   Allocate and initialize a new env.
 *   On success, the new env is stored at '*new'.
 *
 * Pre-Condition:
 *   If the new env doesn't have parent, 'parent_id' should be zero.
 *   'env_init' has been called before this function.
 *
 * Post-Condition:
 *   return 0 on success, and basic fields of the new Env are set up.
 *   return < 0 on error, if no free env, no free asid, or 'env_setup_vm' failed.
 *
 * Hints:
 *   You may need to use these functions or macros:
 *     'LIST_FIRST', 'LIST_REMOVE', 'mkenvid', 'asid_alloc', 'env_setup_vm'
 *   Following fields of Env should be set up:
 *     'env_id', 'env_asid', 'env_parent_id', 'env_tf.regs[29]', 'env_tf.cp0_status',
 *     'env_user_tlb_mod_entry', 'env_runs'
 */
// 获取一个进程控制块，并设置相应的属性
int env_alloc(struct Env **new, u_int parent_id) {
  int func_info;
  struct Env *env;

  // 获取一个空闲进程控制块
  if (LIST_EMPTY(&env_free_list)) {
    return -E_NO_FREE_ENV;
  }
  env = LIST_FIRST(&env_free_list);

  // 初始化新进程的虚拟地址空间
  if ((func_info = env_setup_vm(env)) != 0) {
    return func_info;
  }

  /* Step 3: Initialize these fields for the new Env with appropriate values:
   *   'env_user_tlb_mod_entry' (lab4), 'env_runs' (lab6), 'env_id' (lab3), 'env_asid' (lab3),
   *   'env_parent_id' (lab3)
   */
  env->env_user_tlb_mod_entry = 0;  // for lab4
  env->env_runs = 0;	              // for lab6
  // 设置env块的唯一id
  env->env_id = mkenvid(env);
  env->env_parent_id = parent_id;
  if ((func_info = asid_alloc(&env->env_asid)) != 0) {
    return func_info;
  }

  // 设置进程相关的属性
  /* Step 4: Initialize the sp and 'cp0_status' in 'e->env_tf'.
   *   Set the EXL bit to ensure that the processor remains in kernel mode during context
   * recovery. Additionally, set UM to 1 so that when ERET unsets EXL, the processor
   * transitions to user mode.
   */
  // -IE：中断是否开启
  // -IM7：7 号中断（时钟中断）是否可以被响应
  // - 当且仅当EXL被设置为0且UM 被设置为1时，处理器处于用户模式
  // - 其它所有情况下，处理器均处于内核模式下
  // - 栈寄存器是第29号寄存器，是用户栈，不是内核栈
  env->env_tf.cp0_status = STATUS_IM7 | STATUS_IE | STATUS_EXL | STATUS_UM;
  // Reserve space for 'argc' and 'argv'.
  env->env_tf.regs[29] = USTACKTOP - sizeof(int) - sizeof(char **);

  // 如果上述操作都成功，则从空闲进程链表中移除该进程块
  LIST_REMOVE(env, env_link);
  *new = env;
  
  return 0;
}

/* Overview:
 *   Load a page into the user address space of an env with permission 'perm'.
 *   If 'src' is not NULL, copy the 'len' bytes from 'src' into 'offset' at this page.
 *
 * Pre-Condition:
 *   'offset + len' is not larger than 'PAGE_SIZE'.
 *
 * Hint:
 *   The address of env structure is passed through 'data' from 'elf_load_seg', where this function
 *   works as a callback.
 *
 * Note:
 *   This function involves loading executable code to memory. After the completion of load
 *   procedures, D-cache and I-cache writeback/invalidation MUST be performed to maintain cache
 *   coherence, which MOS has NOT implemented. This may result in unexpected behaviours on real
 *   CPUs! QEMU doesn't simulate caching, allowing the OS to function correctly.
 */
// load_icode_mapper是回调函数的具体实现，用于完成单个页面的加载过程
static int load_icode_mapper(void *data, u_long virtual_address, size_t offset, 
                             u_int permission, const void *src, size_t len) {
  // 将 data 还原为进程控制块
  struct Env *env = (struct Env *)data;
  struct Page *page;
  int func_info;

  /* Step 1: Allocate a page with 'page_alloc'. */
  /* Exercise 3.5: Your code here. (1/2) */
  if((func_info = page_alloc(&page)) != 0) {
    return func_info;
  }

  /* Step 2: If 'src' is not NULL, copy the 'len' bytes started at 'src' into 'offset' at this
   * page. */
  // Hint: You may want to use 'memcpy'.
  if (src != NULL) {
    /* Exercise 3.5: Your code here. (2/2) */
    memcpy((void *)(page2kva(page) + offset), src, len);
  }

  /* Step 3: Insert 'p' into 'env->env_pgdir' at 'va' with 'perm'. */
  // 将虚拟地址映射到页
  return page_insert(env->env_pgdir, env->env_asid, page, virtual_address, permission);
}

/* Overview:
 *   Load program segments from 'binary' into user space of the env 'e'.
 *   'binary' points to an ELF executable image of 'size' bytes, which contains both text and data
 *   segments.
 */
// 加载可执行文件binary到进程e的内存中
static void load_icode(struct Env *env, const void *binary, size_t size) {
  /* Step 1: Use 'elf_from' to parse an ELF header from 'binary'. */
  // 解析地址对应的文件是否为ELF类型
  const Elf32_Ehdr *ehdr = elf_from(binary, size);
  if (!ehdr) {
    panic("bad elf at %x", binary);
  }

  /* Step 2: Load the segments using 'ELF_FOREACH_PHDR_OFF' and 'elf_load_seg'.
   * As a loader, we just care about loadable segments, so parse only program headers here.
   */
  size_t ph_off;
  // 遍历所有程序头表
  ELF_FOREACH_PHDR_OFF (ph_off, ehdr) {
    Elf32_Phdr *ph = (Elf32_Phdr *)(binary + ph_off);
    // 该类型说明其对应的程序需要被加载到内存中
    if (ph->p_type == PT_LOAD) {
      // load_icode_mapper是回调函数的具体实现，用于完成单个页面的加载过程
      panic_on(elf_load_seg(ph, binary + ph->p_offset, load_icode_mapper, env));
    }
  }

  // 将进程控制块中trap frame的epc cp0寄存器的值设置为ELF文件中设定的程序入口地址
  // 指示了进程恢复运行时PC应恢复到的位置
  env->env_tf.cp0_epc = ehdr->e_entry;
}

/* Overview:
 *   Create a new env with specified 'binary' and 'priority'.
 *   This is only used to create early envs from kernel during initialization, before the
 *   first created env is scheduled.
 *
 * Hint:
 *   'binary' is an ELF executable image in memory.
 */
struct Env *env_create(const void *binary, size_t size, int priority) {
  struct Env *env;
  /* Step 1: Use 'env_alloc' to alloc a new env, with 0 as 'parent_id'. */
  /* Exercise 3.7: Your code here. (1/3) */
  env_alloc(&env,0);

  /* Step 2: Assign the 'priority' to 'e' and mark its 'env_status' as runnable. */
  /* Exercise 3.7: Your code here. (2/3) */
  env->env_pri =  priority;
  env->env_status = ENV_RUNNABLE;

  /* Step 3: Use 'load_icode' to load the image from 'binary', and insert 'e' into
   * 'env_sched_list' using 'TAILQ_INSERT_HEAD'. */
  /* Exercise 3.7: Your code here. (3/3) */
  load_icode(env, binary, size);
	TAILQ_INSERT_HEAD(&env_sched_list, env, env_sched_link);

  return env;
}

/* Overview:
 *  Free env e and all memory it uses.
 */
void env_free(struct Env *e) {
  Pte *pt;
  u_int pdeno, pteno, pa;

  /* Hint: Note the environment's demise.*/
  printk("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, e->env_id);

  /* Hint: Flush all mapped pages in the user portion of the address space */
  for (pdeno = 0; pdeno < PDX(UTOP); pdeno++) {
    /* Hint: only look at mapped page tables. */
    if (!(e->env_pgdir[pdeno] & PTE_V)) {
      continue;
    }
    /* Hint: find the pa and va of the page table. */
    pa = PTE_ADDR(e->env_pgdir[pdeno]);
    pt = (Pte *)KADDR(pa);
    /* Hint: Unmap all PTEs in this page table. */
    for (pteno = 0; pteno <= PTX(~0); pteno++) {
      if (pt[pteno] & PTE_V) {
        page_remove(e->env_pgdir, e->env_asid,
              (pdeno << PDSHIFT) | (pteno << PGSHIFT));
      }
    }
    /* Hint: free the page table itself. */
    e->env_pgdir[pdeno] = 0;
    page_decref(pa2page(pa));
    /* Hint: invalidate page table in TLB */
    tlb_invalidate(e->env_asid, UVPT + (pdeno << PGSHIFT));
  }
  /* Hint: free the page directory. */
  page_decref(pa2page(PADDR(e->env_pgdir)));
  /* Hint: free the ASID */
  asid_free(e->env_asid);
  /* Hint: invalidate page directory in TLB */
  tlb_invalidate(e->env_asid, UVPT + (PDX(UVPT) << PGSHIFT));
  /* Hint: return the environment to the free list. */
  e->env_status = ENV_FREE;
  LIST_INSERT_HEAD((&env_free_list), (e), env_link);
  TAILQ_REMOVE(&env_sched_list, (e), env_sched_link);
}

/* Overview:
 *  Free env e, and schedule to run a new env if e is the current env.
 */
void env_destroy(struct Env *e) {
  /* Hint: free e. */
  env_free(e);

  /* Hint: schedule to run a new environment. */
  if (curenv == e) {
    curenv = NULL;
    printk("i am killed ... \n");
    schedule(1);
  }
}

// WARNING BEGIN: DO NOT MODIFY FOLLOWING LINES!
#ifdef MOS_PRE_ENV_RUN
#include <generated/pre_env_run.h>
#endif
// WARNING END

extern void env_pop_tf(struct Trapframe *tf, u_int asid) __attribute__((noreturn));

/* Overview:
 *   Switch CPU context to the specified env 'e'.
 *
 * Post-Condition:
 *   Set 'e' as the current running env 'curenv'.
 *
 * Hints:
 *   You may use these functions: 'env_pop_tf'.
 */
void env_run(struct Env *env) {
  assert(env->env_status == ENV_RUNNABLE);
  // WARNING BEGIN: DO NOT MODIFY FOLLOWING LINES!
#ifdef MOS_PRE_ENV_RUN
  MOS_PRE_ENV_RUN_STMT
#endif
  // WARNING END

  /* Step 1:
   *   If 'curenv' is NULL, this is the first time through.
   *   If not, we may be switching from a previous env, so save its context into
   *   'curenv->env_tf' first.
   */
  // 此时全局变量curenv中还是切换前的进程控制块，保存该进程的上下文
  // 将栈帧中trap frame的信息转换为 Trapframe存储在 env_tf中
  // 存储在 [KSTACKTOP - 1, KSTACKTOP) 的范围内，参考关于 SAVE_ALL 宏的内容
  if (curenv) {
    curenv->env_tf = *((struct Trapframe *)KSTACKTOP - 1);
  }

  // 切换现在运行的进程
  /* Step 2: Change 'curenv' to 'e'. */
  curenv = env;
  curenv->env_runs++; // lab6

  /* Step 3: Change 'cur_pgdir' to 'curenv->env_pgdir', switching to its address space. */
  /* Exercise 3.8: Your code here. (1/2) */
  // 设置全局变量cur_pgdir为当前进程页目录地址，在TLB重填时将用到该全局变量
  cur_pgdir = curenv->env_pgdir;

  /* Step 4: Use 'env_pop_tf' to restore the curenv's saved context (registers) and return/go
   * to user mode.
   *
   * Hint:
   *  - You should use 'curenv->env_asid' here.
   *  - 'env_pop_tf' is a 'noreturn' function: it restores PC from 'cp0_epc' thus not
   *    returning to the kernel caller, making 'env_run' a 'noreturn' function as well.
   */
  /* Exercise 3.8: Your code here. (2/2) */
  // 根据栈帧还原进程上下文，并运行程序
  // 恢复现场、异常返回
  env_pop_tf(&curenv->env_tf, curenv->env_asid);
}

void env_check() {
  struct Env *pe, *pe0, *pe1, *pe2;
  struct Env_list fl;
  u_long page_addr;
  /* should be able to allocate three envs */
  pe0 = 0;
  pe1 = 0;
  pe2 = 0;
  assert(env_alloc(&pe0, 0) == 0);
  assert(env_alloc(&pe1, 0) == 0);
  assert(env_alloc(&pe2, 0) == 0);

  assert(pe0);
  assert(pe1 && pe1 != pe0);
  assert(pe2 && pe2 != pe1 && pe2 != pe0);

  /* temporarily steal the rest of the free envs */
  fl = env_free_list;
  /* now this env_free list must be empty! */
  LIST_INIT(&env_free_list);

  /* should be no free memory */
  assert(env_alloc(&pe, 0) == -E_NO_FREE_ENV);

  /* recover env_free_list */
  env_free_list = fl;

  printk("pe0->env_id %d\n", pe0->env_id);
  printk("pe1->env_id %d\n", pe1->env_id);
  printk("pe2->env_id %d\n", pe2->env_id);

  assert(pe0->env_id == 2048);
  assert(pe1->env_id == 4097);
  assert(pe2->env_id == 6146);
  printk("env_init() work well!\n");

  /* 'UENVS' and 'UPAGES' should have been correctly mapped in *template* page directory
   * 'base_pgdir'. */
  for (page_addr = 0; page_addr < npage * sizeof(struct Page); page_addr += PAGE_SIZE) {
    assert(va2pa(base_pgdir, UPAGES + page_addr) == PADDR(pages) + page_addr);
  }
  for (page_addr = 0; page_addr < NENV * sizeof(struct Env); page_addr += PAGE_SIZE) {
    assert(va2pa(base_pgdir, UENVS + page_addr) == PADDR(envs) + page_addr);
  }
  /* check env_setup_vm() work well */
  printk("pe1->env_pgdir %x\n", pe1->env_pgdir);

  assert(pe2->env_pgdir[PDX(UTOP)] == base_pgdir[PDX(UTOP)]);
  assert(pe2->env_pgdir[PDX(UTOP) - 1] == 0);
  printk("env_setup_vm passed!\n");

  printk("pe2`s sp register %x\n", pe2->env_tf.regs[29]);

  /* free all env allocated in this function */
  TAILQ_INSERT_TAIL(&env_sched_list, pe0, env_sched_link);
  TAILQ_INSERT_TAIL(&env_sched_list, pe1, env_sched_link);
  TAILQ_INSERT_TAIL(&env_sched_list, pe2, env_sched_link);

  env_free(pe2);
  env_free(pe1);
  env_free(pe0);

  printk("env_check() succeeded!\n");
}

void envid2env_check() {
  struct Env *pe, *pe0, *pe2;
  assert(env_alloc(&pe0, 0) == 0);
  assert(env_alloc(&pe2, 0) == 0);
  int re;
  pe2->env_status = ENV_FREE;
  re = envid2env(pe2->env_id, &pe, 0);

  assert(re == -E_BAD_ENV);

  pe2->env_status = ENV_RUNNABLE;
  re = envid2env(pe2->env_id, &pe, 0);

  assert(pe->env_id == pe2->env_id && re == 0);

  curenv = pe0;
  re = envid2env(pe2->env_id, &pe, 1);
  assert(re == -E_BAD_ENV);
  printk("envid2env() work well!\n");
}
