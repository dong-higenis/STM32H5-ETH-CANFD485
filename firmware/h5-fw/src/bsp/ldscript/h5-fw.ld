/*
 * h5-fw.ld  -  STM32H563RI 애플리케이션 링커 스크립트
 *
 * 부트로더(h5-boot.ld)와 다른 점은 Flash 주소뿐이다.
 *
 *   0x08010000  TAG     1KB   <- 빌드 결과물에는 없다. tools/fw_tag.py 가 앞에 붙인다.
 *   0x08010400  VECTOR  1KB   <- 이 이미지(.bin)의 시작
 *   0x08010800  VER     1KB   <- 버전 구조체 (이미지 기준 +0x400, GUI 가 읽는 위치)
 *   0x08010C00  FLASH         <- 코드
 *
 * Vector table 정렬: Cortex-M33 은 VTOR 주소가 "표 크기를 2의 거듭제곱으로 올린 값"의
 * 배수여야 한다. H563 표는 588바이트 -> 1024 정렬 필요 -> 0x08010400 은 1024 의 배수이므로 OK.
 */

ENTRY(Reset_Handler)

MEMORY
{
  VECTOR  (rx)  : ORIGIN = 0x08010400, LENGTH = 1K
  VER     (rx)  : ORIGIN = 0x08010800, LENGTH = 1K
  FLASH   (rx)  : ORIGIN = 0x08010C00, LENGTH = 448K - 3K    /* 1MB 칩: 앱 슬롯 448KB (common/mem_map.h) */

  NOINIT  (xrw) : ORIGIN = 0x20000000, LENGTH = 8K
  RAM     (xrw) : ORIGIN = 0x20002000, LENGTH = 640K - 8K
}

_estack         = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size  = 0x400;
_Min_Stack_Size = 0x1000;

SECTIONS
{
  .fw_flash_begin (NOLOAD) :
  {
    _fw_flash_begin = .;
  } >VECTOR

  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >VECTOR

  .version :
  {
    . = ALIGN(4);
    KEEP(*(.version))
    . = ALIGN(4);
  } >VER

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH

  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  .noinit (NOLOAD) :
  {
    . = ALIGN(4);
    KEEP(*(.noinit))
    KEEP(*(.noinit*))
    . = ALIGN(4);
  } >NOINIT

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
