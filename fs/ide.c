/*
 * operations on IDE disk.
 */

#include "serv.h"
#include <lib.h>
#include <malta.h>
#include <mmu.h>

/* Overview:
 *   Wait for the IDE device to complete previous requests and be ready
 *   to receive subsequent requests.
 */
// 等待IDE的操作就绪，当没有就绪时忙等待
static uint8_t wait_ide_ready() {
  uint8_t flag;
  while (1) {
    // 读取相应信息到flag
    panic_on(syscall_read_dev(&flag, MALTA_IDE_STATUS, 1));
    if ((flag & MALTA_IDE_BUSY) == 0) {
      break;
    }
    // 避免CPU轮询，对CPU更友好
    syscall_yield();
  }

  return flag;
}

/* Overview:
 *  read data from IDE disk. First issue a read request through
 *  disk register and then copy data from disk buffer
 *  (512 bytes, a sector) to destination array.
 *
 * Parameters:
 *  disk_no: disk number.
 *  sec_no: start sector number.
 *  dst: destination for data read from IDE disk.
 *  seco_num: the number of sectors to read.
 *
 * Post-Condition:
 *  Panic if any error occurs. (you may want to use 'panic_on')
 */
// 用户态下对磁盘的读操作
void ide_read(u_int disk_no,  // 磁盘号
              u_int sec_no,   // 扇区号
              void *dst,      // 读取的地址
              u_int seco_num  // 读取n个扇区
  ) {
  u_int seco_max = seco_num + sec_no;
  u_int address_offest = 0;
  uint8_t status_info;

  panic_on(disk_no >= 2);

  // 依次读取n个扇区
  while (sec_no < seco_max) {
    // 等待IDE设备就绪
    status_info = wait_ide_ready();

    // 设置操作扇区数目  NSECT
    status_info = 1;
    panic_on(syscall_write_dev(&status_info, MALTA_IDE_NSECT, 1));

    // 设置操作扇区号的[7:0]位  LBAL
    status_info = sec_no & 0xff;
    panic_on(syscall_write_dev(&status_info, MALTA_IDE_LBAL, 1));

    // 设置操作扇区号的[15:8]位  IBAM
    status_info = (sec_no >> 8) & 0xff;
    panic_on(syscall_write_dev(&status_info, MALTA_IDE_LBAM, 1));

    // 设置操作扇区号的[23:16]位  LBAH
    status_info = (sec_no >> 16) & 0xff;
    panic_on(syscall_write_dev(&status_info, MALTA_IDE_LBAH, 1));

    // 设置操作扇区号的[27:24]位，设置扇区寻址模式、磁盘编号
    status_info = ((sec_no >> 24) & 0x0f) | MALTA_IDE_LBA | (disk_no << 4);
    panic_on(syscall_write_dev(&status_info, MALTA_IDE_DEVICE, 1));

    // 设置IDE设备为读状态
    status_info = MALTA_IDE_CMD_PIO_READ;
    panic_on(syscall_write_dev(&status_info, MALTA_IDE_STATUS, 1));

    // 等待IDE设备就绪
    status_info = wait_ide_ready();

    // 循环读取完成整个扇区的数据，每次仅能读取4字节
    for (int i = 0; i < SECT_SIZE / 4; i++) {
      panic_on(syscall_read_dev(dst + address_offest + i * 4, MALTA_IDE_DATA, 4));
    }

    // 检查IDE设备状态
    panic_on(syscall_read_dev(&status_info, MALTA_IDE_STATUS, 1));

    // 进入下一个扇区的读取
    address_offest += SECT_SIZE;
    sec_no += 1;
  }
}

/* Overview:
 *  write data to IDE disk.
 *
 * Parameters:
 *  disk_no: disk number.
 *  sec_no: start sector number.
 *  src: the source data to write into IDE disk.
 *  seco_num: the number of sectors to write.
 *
 * Post-Condition:
 *  Panic if any error occurs.
 */
void ide_write(u_int disk_no, u_int sec_no, void *src, u_int seco_num) {
  u_int seco_max = seco_num + sec_no;
  u_int address_offest = 0;
  uint8_t status_info;

  panic_on(disk_no >= 2);

  // 依次写入扇区
  while (sec_no < seco_max) {
    // 等待IDE设备就绪
    status_info = wait_ide_ready();

    // 设置操作扇区数目
    status_info = 1;
    panic_on(syscall_write_dev(&status_info, MALTA_IDE_NSECT, 1));

    // 设置操作扇区号的[7:0]位  IBAL
    status_info = sec_no & 0xff;
    panic_on(syscall_write_dev(&status_info, MALTA_IDE_LBAL, 1));

    // 设置操作扇区号的[15:8]位  IBAM
    status_info = (sec_no >> 8) & 0xff;
    panic_on(syscall_write_dev(&status_info, MALTA_IDE_LBAM, 1));

    // 设置操作扇区号的[23:16]位  IBAH
    status_info = (sec_no >> 16) & 0xff;
    panic_on(syscall_write_dev(&status_info, MALTA_IDE_LBAH, 1));

    // 设置操作扇区号的[27:24]位，设置扇区寻址模式、磁盘编号
    status_info = ((sec_no >> 24) & 0x0f) | MALTA_IDE_LBA | (disk_no << 4);
    panic_on(syscall_write_dev(&status_info, MALTA_IDE_DEVICE, 1));

    // 设置IDE设备为写状态
    status_info = MALTA_IDE_CMD_PIO_WRITE;
    panic_on(syscall_write_dev(&status_info, MALTA_IDE_STATUS, 1));

    // 等待IDE设备就绪
    status_info = wait_ide_ready();

    // 写入数据，一次只能写入4个字节
    for (int i = 0; i < SECT_SIZE / 4; i++) {
      panic_on(syscall_write_dev(src + address_offest + i * 4, MALTA_IDE_DATA, 4));
    }

    // 检查 IDE 设备状态
    panic_on(syscall_read_dev(&status_info, MALTA_IDE_STATUS, 1));

    // 进入下一个扇区的写入
    address_offest += SECT_SIZE;
    sec_no += 1;
  }
}
