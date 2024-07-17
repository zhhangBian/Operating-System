#include <asm/asm.h>
#include <env.h>
#include <pmap.h>
#include <printk.h>
#include <sched.h>
#include <trap.h>

/*
 * Note:
 * When build with 'make test lab=?_?', we will replace your 'mips_init' with a generated one from
 * 'tests/lab?_?'.
 */

#ifdef MOS_INIT_OVERRIDDEN
#include <generated/init_override.h>
#else

/*
 * Note:
 *   When booting the OS, the bootloader passes certain parameters to the OS
 * through registers to provide information regarding the hardware configuration. One
 * of these parameters is ram_low_size, which represents the available physical memory
 * size (in bytes) of the system. Definitions of these parameters are typically agreed upon by
 * the hardware manufacturer and OS developers. YAMON is released by MIPS Inc. as the official
 * bootloader on the malta board, which is possibly the initial standard implementation. These
 * definitions of startup parameters are detailed in MIPS YAMON User’s Manual (Section 5.1,
 * Application Interface - Entry). The manual can be downloaded via the following link:
 * http://os.buaa.edu.cn/public/reference/YAMON_USM-02.19.pdf
 *
 *   In the start.S file, the a0-a3 registers used for passing parameters are not modified, so when
 * calling the mips_init function, these parameters can be directly accessed.
 *
 *   In QEMU emulation environment, a simplified bootloader has been coded to approach YAMON's
 * functionality, particularly in passing the startup parameters. The relevant QEMU code can be
 * viewed at the following link: https://github.com/qemu/qemu/blob/v6.2.0/hw/mips/malta.c#L818-L997
 * U-Boot supports the malta board as well, and we can find the startup parameters it passes to the
 * OS in codes. The relevant U-Boot code can be viewed at the following link:
 * https://github.com/u-boot/u-boot/blob/v2023.10/arch/mips/lib/bootm.c#L274C1-L302C2
 */

void mips_init(u_int argc, char **argv, char **penv, u_int ram_low_size) {
  printk("init.c:\tmips_init() is called\n");

  // 检测物理内存大小，确认页的数量
  mips_detect_memory(ram_low_size);
  // 初始化虚拟内存：创建了相应数量的页控制模块，所有物理页都有对应的唯一的控制模块
  mips_vm_init();
  // 对pages进行初始化，维护管理空闲页的页控制块的链表
  page_init();

  // 对envs进行初始化，维护空闲进程列表
  // 并创建模板页目录，方便后续使用
  env_init();

  // 在内核初始化时设置两个进程，并开始运行（创建即运行）
  ENV_CREATE_PRIORITY(user_bare_loop, 1);
  ENV_CREATE_PRIORITY(user_bare_loop, 2);

  // lab4:
  ENV_CREATE(user_tltest);
  ENV_CREATE(user_fktest);
  ENV_CREATE(user_pingpong);

  // 用于启动shell进程，必须是第一个进程
  // 整个操作系统中除文件系统服务进程外所有进程的共同祖先进程
  ENV_CREATE(user_icode);
  // lab5:
  ENV_CREATE(user_fstest);
  // 文件系统服务进程，必须是第二个进程
  ENV_CREATE(fs_serv);
  ENV_CREATE(user_devtst);

  // 漫长的启动过程结束了，一切都清静了
  schedule(0);
}

#endif
