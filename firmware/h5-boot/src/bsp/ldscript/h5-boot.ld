/*
 * h5-boot.ld  -  STM32H563RI 부트로더 링커 스크립트
 *
 * ST 템플릿(STM32H563xx_FLASH.ld)을 바탕으로 아래만 바꿨다.
 *   1) Flash 를 64KB 로 제한         : 부트로더 영역을 넘으면 링크 에러가 나도록
 *   2) VECTOR / VER 영역 분리        : 버전 정보를 항상 +0x400 에 고정
 *   3) RAM 앞 8KB 를 NOINIT 로 분리  : 리셋해도 지워지지 않는 boot <-> fw 공유 영역
 *
 * 주소를 바꾸면 firmware/common/mem_map.h 도 같이 바꿔야 한다.
 */

ENTRY(Reset_Handler)

MEMORY
{
  VECTOR  (rx)  : ORIGIN = 0x08000000, LENGTH = 1K
  VER     (rx)  : ORIGIN = 0x08000400, LENGTH = 1K
  FLASH   (rx)  : ORIGIN = 0x08000800, LENGTH = 64K - 2K

  NOINIT  (xrw) : ORIGIN = 0x20000000, LENGTH = 8K
  RAM     (xrw) : ORIGIN = 0x20002000, LENGTH = 640K - 8K
}

_estack         = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size  = 0x400;
_Min_Stack_Size = 0x1000;

SECTIONS
{
  /* 이 이미지의 시작 주소를 심볼로 남긴다 (firm_ver_t.firm_addr) */
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

  /* .data 는 RAM 에서 실행되지만 초기값은 Flash 에 저장된다. startup 이 복사한다. */
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

  /* startup 이 건드리지 않는 영역. 리셋 전 값이 그대로 남는다. */
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
