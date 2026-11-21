MEMORY { FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 64K  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 20K }
_estack = ORIGIN(RAM) + LENGTH(RAM);
ENTRY(Reset_Handler)
SECTIONS {
  .text : { KEEP(*(.isr_vector)) *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
  /DISCARD/ : { *(.ARM.exidx*) *(.comment) }
}
