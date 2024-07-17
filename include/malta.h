#ifndef MALTA_H
#define MALTA_H

/*
 * QEMU MMIO address definitions.
 */
#define MALTA_PCIIO_BASE 0x18000000
#define MALTA_FPGA_BASE 0x1f000000

/*
 * 16550 Serial UART device definitions.
 */
#define MALTA_SERIAL_BASE (MALTA_PCIIO_BASE + 0x3f8)
#define MALTA_SERIAL_DATA (MALTA_SERIAL_BASE + 0x0)
#define MALTA_SERIAL_LSR (MALTA_SERIAL_BASE + 0x5)
#define MALTA_SERIAL_DATA_READY 0x1
#define MALTA_SERIAL_THR_EMPTY 0x20

/*
 * Intel PIIX4 IDE Controller device definitions.
 * Hardware documentation available at
 * https://www.intel.com/Assets/PDF/datasheet/290562.pdf
 */
#define MALTA_IDE_BASE (MALTA_PCIIO_BASE + 0x01f0)

// 读/写：向磁盘中读/写数据，从0字节开始逐个读出/写
#define MALTA_IDE_DATA (MALTA_IDE_BASE + 0x00)

// 读：设备错误信息；写：设置IDE命令的特定参数
#define MALTA_IDE_ERR (MALTA_IDE_BASE + 0x01)

// 写：设置一次需要操作的扇区数量
#define MALTA_IDE_NSECT (MALTA_IDE_BASE + 0x02)

// 写：设置目标扇区号的[7:0]位  LBAL
#define MALTA_IDE_LBAL (MALTA_IDE_BASE + 0x03)

// 写：设置目标扇区号的[15:8]位  LBAM
#define MALTA_IDE_LBAM (MALTA_IDE_BASE + 0x04)

// 写：设置目标扇区号的[23:16]位  LBAH
#define MALTA_IDE_LBAH (MALTA_IDE_BASE + 0x05)

// 写：设置目标扇区号的[27:24]位，配置扇区寻址模式 CHS/LBA，设置要操作的磁盘编号
#define MALTA_IDE_DEVICE (MALTA_IDE_BASE + 0x06)

// 读：获取设备状态；写：配置设备工作状态
#define MALTA_IDE_STATUS (MALTA_IDE_BASE + 0x07)

#define MALTA_IDE_LBA 0xE0
#define MALTA_IDE_BUSY 0x80

// IDE设备为读状态
#define MALTA_IDE_CMD_PIO_READ 0x20  /* Read sectors with retry */

// IDE设备为写状态
#define MALTA_IDE_CMD_PIO_WRITE 0x30 /* write sectors with retry */

/*
 * MALTA Power Management device definitions.
 */
#define MALTA_FPGA_HALT (MALTA_FPGA_BASE + 0x500)

#endif
