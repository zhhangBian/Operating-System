#ifndef _IO_H_
#define _IO_H_

#include <pmap.h>

// 读取kseg1区的数据，通过指针类型的准换实现读取数字位宽的转换

/* Overview:
 *   Read a byte (8 bits) at the specified physical address `pyhsical_address`.
 *
 * Pre-Condition:
 *  `pyhsical_address` is a valid physical address.
 *
 * Parameters:
 *  pyhsical_address: The physical address to be read.
 *
 * Return the 8-bit value stored at `pyhsical_address`.
 *
 * Example:
 *  uint8_t data = ioread8(pa); // read a byte at physical address `pa`.
 */
static inline uint8_t ioread8(u_long pyhsical_address) {
  return *(volatile uint8_t *)(pyhsical_address | KSEG1);
}

/* Overview:
 *   Read half a word (16 bits) at the specified physical address `pyhsical_address`.
 *   Return the read result.
 */
static inline uint16_t ioread16(u_long pyhsical_address) {
  return *(volatile uint16_t *)(pyhsical_address | KSEG1);
}

/* Overview:
 *   Read a word (32 bits) at the specified physical address `pyhsical_address`.
 *   Return the read result.
 */
static inline uint32_t ioread32(u_long pyhsical_address) {
  return *(volatile uint32_t *)(pyhsical_address | KSEG1);
}

// 写kseg1区的数据，通过指针类型的准换实现写数字位宽的转换

/* Overview:
 *   Write a byte (8 bits of data) to the specified physical address `pyhsical_address`.
 *
 * Pre-Condition:
 *  `pyhsical_address` is a valid physical address.
 *
 * Post-Condition:
 *   The byte at the `pyhsical_address` is overwritten by `data`.
 *
 * Parameters:
 *   data: The data to be written.
 *   pyhsical_address: The physical address to be written.
 *
 * Example:
 *   iowrite8(data, pa); // write `data` to physical address `pa`.
 */
static inline void iowrite8(uint8_t data, u_long pyhsical_address) {
  *(volatile uint8_t *)(pyhsical_address | KSEG1) = data;
}

/* Overview:
 *   Write half a word (16 bits of data) to the specified physical address `pyhsical_address`.
 */
static inline void iowrite16(uint16_t data, u_long pyhsical_address) {
  *(volatile uint16_t *)(pyhsical_address | KSEG1) = data;
}

/* Overview:
 *   Write a word (32 bits of data) to the specified physical address `pyhsical_address`.
 */
static inline void iowrite32(uint32_t data, u_long pyhsical_address) {
  *(volatile uint32_t *)(pyhsical_address | KSEG1) = data;
}

#endif
