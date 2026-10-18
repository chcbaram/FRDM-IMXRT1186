/*
 * rt1180-fw CM7 — CM7 ITCM 에서 실행
 *
 * 근거: docs/25-cm7-boot.md, docs/02-memory-map.md 1절
 *
 *   CM7 이 본 주소           CM33 이 본 주소
 *   0x0000_0000  ITCM 256K   0x303C_0000   벡터 · 코드 · .data 초기값 (CM33 이 복사)
 *   0x2000_0000  DTCM 256K   0x3040_0000   .data · .bss · 스택
 *   0x2050_0000  OCRAM2      0x2050_0000   공유 블록 (cpu/shared/shared.h)
 *   0x2800_0000  HyperRAM 6M 0x2800_0000   .psram_bss (CM33 몫은 뒤 2 MB, docs/27)
 *
 * 이미지 전체(벡터부터 .data 초기값까지)가 ITCM 한 덩어리다. CM33 은 QSPI CM7 슬롯에서
 * 이 덩어리를 그대로 복사하고, CM7 startup 이 .data 를 ITCM 에서 DTCM 으로 옮긴다.
 * INITVTOR 는 0 이다 (cm7Prepare 의 Prepare_CM7(0)).
 */

ENTRY(Reset_Handler)

__stack_size__ = DEFINED(__stack_size__) ? __stack_size__ : 0x2000;
__heap_size__  = DEFINED(__heap_size__)  ? __heap_size__  : 0x1000;

MEMORY
{
  ITCM       (RWX) : ORIGIN = 0x00000000, LENGTH = 256K
  DTCM       (RW)  : ORIGIN = 0x20000000, LENGTH = 256K
  SHARED     (RW)  : ORIGIN = 0x20500000, LENGTH = 4K
  PSRAM      (RW)  : ORIGIN = 0x28000000, LENGTH = 6M      /* HyperRAM 앞 6 MB : CM7 몫 (SDK 와 같은 분할) */
}

SECTIONS
{
  .interrupts :
  {
    . = ALIGN(4);
    __Vectors = .;
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > ITCM

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.rodata)
    *(.rodata*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
  } > ITCM

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > ITCM
  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > ITCM

  .preinit_array :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } > ITCM
  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } > ITCM
  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } > ITCM

  __etext = .;

  __data_flash_start = .;
  .data : AT(__data_flash_start)
  {
    . = ALIGN(4);
    __data_start__ = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    __data_end__ = .;
  } > DTCM

  __image_end = __data_flash_start + (__data_end__ - __data_start__);
  ASSERT(__image_end <= ORIGIN(ITCM) + LENGTH(ITCM), "CM7 image exceeds ITCM")

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    __bss_start__ = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end__ = .;
  } > DTCM

  .heap (NOLOAD) :
  {
    . = ALIGN(8);
    __HeapBase = .;
    end = .;
    . += __heap_size__;
    __HeapLimit = .;
  } > DTCM

  .stack (NOLOAD) :
  {
    . = ALIGN(8);
    __StackStart = .;
    . += __stack_size__;
  } > DTCM

  __StackTop   = ORIGIN(DTCM) + LENGTH(DTCM);
  __StackLimit = __StackTop - __stack_size__;
  PROVIDE(__stack = __StackTop);
  ASSERT(__StackLimit >= __StackStart, "DTCM overflowed with data, heap and stack")

  .shared (NOLOAD) :
  {
    KEEP(*(.shared))
    . = ALIGN(4);
  } > SHARED

  /* HyperRAM 의 이 코어 몫 (docs/27). NOLOAD 라 기록 · startup 이 건드리지 않는다.
     ROM 이 XMCD 로 HyperRAM 을 연 뒤 psramInit() 이 0 으로 채운다. 32 B = CM7 캐시 줄. */
  .psram_bss (NOLOAD) :
  {
    . = ALIGN(32);
    __psram_bss_start__ = .;
    *(.psram_bss)
    *(.psram_bss*)
    . = ALIGN(32);
    __psram_bss_end__ = .;
  } > PSRAM

  __psram_region_start = ORIGIN(PSRAM);
  __psram_region_size  = LENGTH(PSRAM);

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
