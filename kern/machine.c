#include <malta.h>
#include <mmu.h>
#include <printk.h>

// 使用内存映射IO（MMOI）技术，通过控制台实现字符的输入输出
// 设备寄存器被映射到指定的**物理地址**
// 通过往内存的 0x180003F8+0xA0000000 地址写入字符，就能在shell中看到对应的输出

/* Overview:
 *   Send a character to the console. If Transmitter Holding Register is currently occupied by
 *   some data, wait until the register becomes available.
 *
 * Pre-Condition:
 *   'ch' is the character to be sent.
 */
void printcharc(char ch) {
  if (ch == '\n') {
    printcharc('\r');
  }
  while (!(*((volatile uint8_t *)(KSEG1 + MALTA_SERIAL_LSR)) & MALTA_SERIAL_THR_EMPTY)) {}
  // 通过往内存的(0x180003F8+0xA0000000) 地址写入字符，实现向控制台的输出
  *((volatile uint8_t *)(KSEG1 + MALTA_SERIAL_DATA)) = ch;
}

/* Overview:
 *   Read a character from the console.
 *
 * Post-Condition:
 *   If the input character data is ready, read and return the character.
 *   Otherwise, i.e. there's no input data available, 0 is returned immediately.
 */
int scancharc(void) {
  if (*((volatile uint8_t *)(KSEG1 + MALTA_SERIAL_LSR)) & MALTA_SERIAL_DATA_READY) {
    return *((volatile uint8_t *)(KSEG1 + MALTA_SERIAL_DATA));
  }
  return 0;
}

/* Overview:
 *   Halt/Reset the whole system. Write the magic value GORESET(0x42) to SOFTRES register of the
 *   FPGA on the Malta board, initiating a board reset. In QEMU emulator, emulation will stop
 *   instead of rebooting with the parameter '-no-reboot'.
 *
 * Post-Condition:
 *   If the current device doesn't support such halt method, print a warning message and enter
 *   infinite loop.
 */
void halt(void) {
  *(volatile uint8_t *)(KSEG1 + MALTA_FPGA_HALT) = 0x42;
  printk("machine.c:\thalt is not supported in this machine!\n");
  while (1) {
  }
}
