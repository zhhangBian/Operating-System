/*
 * Set the architecture to mips.
 */
OUTPUT_ARCH(mips)

/*
 * Set the ENTRY point of the program to _start.
 */
ENTRY(_start)

SECTIONS {
/*
 *  为操作系统增加异常分发功能
 *  .text.exc_gen_entry 段和.text.tlb_miss_entry 段需要被链接器放到特定的位置。
 *  在4Kc 中，这两个段分别要求放到地址0x80000180 和0x80000000 处
 *  它们是异常处理程序的入口地址。
*/

  . = 0x80000000;
  .tlb_miss_entry : {
    *(.text.tlb_miss_entry)
  }

  . = 0x80000180;
	.exc_gen_entry : {
		*(.text.exc_gen_entry)
	}

	. = 0x80020000;
	
	.text : { *(.text) }

	.data : { *(.data) }
	
	bss_start = .;
	.bss : { *(.bss) }
	
	bss_end = .;
	. = 0x80400000;
	end = . ;
}
