OUTPUT_ARCH( "riscv" )
ENTRY( _start )

MEMORY
{
  ram  (wxa) : ORIGIN = 0x00000000, LENGTH = 256M
}

SECTIONS
{
  .text : {
    . = ALIGN(4);
    *(.text._start)
    *(.text)
    *(.text.*)
  } > ram

  .rodata : {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata.*)
    *(.srodata)
    *(.srodata.*)
  } > ram

  .data : {
    . = ALIGN(4);
    *(.data)
    *(.data.*)
    *(.sdata)
    *(.sdata.*)
  } > ram

  .bss : {
    . = ALIGN(4);
    *(.bss)
    *(.bss.*)
    *(.sbss)
    *(.sbss.*)
    *(COMMON)
  } > ram

  . = ALIGN(4);
  _end = .;
}
