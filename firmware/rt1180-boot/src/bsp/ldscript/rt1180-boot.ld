/*
 * rt1180-boot — CM33 Code TCM 에서 실행 (non-XIP)
 *
 * 근거: docs/40-boot-architecture.md, docs/41-bootloader.md, RM 12.5.1.2
 *
 *   0x0400_0400  FCB        .boot_hdr.conf       ROM 이 FlexSPI 설정을 읽는다
 *   0x0400_0800  XMCD       .boot_hdr.xmcd_data  ROM 이 HyperRAM 을 설정한다 (docs/27)
 *   0x0400_1000  컨테이너   .boot_hdr.container  load = entry = Code TCM
 *   0x0400_B000  이미지 (플래시 위치, LMA) ── ROM 이 통째로 복사 ──▶ 0x0FFE_0000 (실행 위치, VMA)
 *
 * 이미지 = 벡터 → 코드 → .data 초기값. 플래시와 Code TCM 에서 오프셋이 같다.
 * 그래서 .data 초기값은 플래시가 아니라 Code TCM 안의 사본에서 System TCM 으로 복사한다
 * (__data_flash_start). 부팅이 끝나면 코드는 플래시를 전혀 읽지 않는다.
 *
 *   Code TCM   0x0FFE_0000 128 KB : 이미지 전체
 *   System TCM 0x2000_0000 128 KB : .data · .bss · 힙 · 스택
 */

ENTRY(Reset_Handler)

__stack_size__ = DEFINED(__stack_size__) ? __stack_size__ : 0x2000;
__heap_size__  = DEFINED(__heap_size__)  ? __heap_size__  : 0x1000;

flash_start      = 0x04000000;
slot_size        = 0x00040000;    /* 256 KB : 부트 헤더 + 부트로더 이미지 (docs/40 2절) */
image_offset     = 0x0000B000;    /* SDK 와 같은 위치 */

MEMORY
{
  FCB        (RX)  : ORIGIN = flash_start + 0x0400, LENGTH = 0x200
  XMCD       (RX)  : ORIGIN = flash_start + 0x0800, LENGTH = 0x200
  CONTAINER  (RX)  : ORIGIN = flash_start + 0x1000, LENGTH = 0x2000
  FLASH      (RX)  : ORIGIN = flash_start + image_offset, LENGTH = slot_size - image_offset

  ITCM       (RWX) : ORIGIN = 0x0FFE0000, LENGTH = 128K    /* CM33 Code TCM   */
  DTCM       (RW)  : ORIGIN = 0x20000000, LENGTH = 128K    /* CM33 System TCM */
}

SECTIONS
{
  .fcb :
  {
    KEEP(*(.boot_hdr.conf))
  } > FCB

  .xmcd :
  {
    KEEP(*(.boot_hdr.xmcd_data))
  } > XMCD

  .container :
  {
    KEEP(*(.boot_hdr.container))
  } > CONTAINER

  .interrupts :
  {
    . = ALIGN(4);
    __Vectors = .;
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > ITCM AT> FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(CodeQuickAccess)
    *(.ramfunc*)
    *(.rodata)
    *(.rodata*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
  } > ITCM AT> FLASH

  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } > ITCM AT> FLASH

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > ITCM AT> FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } > ITCM AT> FLASH

  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } > ITCM AT> FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } > ITCM AT> FLASH

  __etext = .;

  /* .data : 초기값은 이미지 끝(플래시 → ROM 이 Code TCM 으로 복사), 실행은 System TCM */
  .data :
  {
    . = ALIGN(4);
    __data_start__ = .;
    *(.data)
    *(.data*)
    *(DataQuickAccess)
    . = ALIGN(4);
    __data_end__ = .;
  } > DTCM AT> FLASH

  /* 이미지 = 벡터부터 .data 초기값 끝까지. 컨테이너가 이 범위를 Code TCM 으로 복사한다. */
  __image_lma_start = LOADADDR(.interrupts);
  __image_end       = LOADADDR(.data) + SIZEOF(.data);

  /* startup 은 이 주소에서 .data 를 복사한다. 플래시 주소가 아니라 Code TCM 안의 사본이다. */
  __data_flash_start = ORIGIN(ITCM) + (LOADADDR(.data) - __image_lma_start);

  /* 컨테이너 이미지 엔트리가 쓰는 값 (bsp/boot/boot_hdr.c) */
  __CONTAINER_IMG_OFFSET = __image_lma_start - ORIGIN(CONTAINER);
  __CONTAINER_IMG_SIZE   = __image_end - __image_lma_start;
  __VECTOR_TABLE         = ADDR(.interrupts);

  ASSERT(ADDR(.interrupts) == ORIGIN(ITCM), "vector table must start Code TCM")
  ASSERT(ADDR(.fini_array) - ORIGIN(ITCM) == LOADADDR(.fini_array) - __image_lma_start,
         "Code TCM and flash offsets differ")
  ASSERT(__CONTAINER_IMG_SIZE <= LENGTH(ITCM), "boot image exceeds Code TCM (128 KB)")

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

  /* 스택은 System TCM 끝에 붙인다. MSPLIM 은 __StackLimit 으로 걸린다. */
  .stack (NOLOAD) :
  {
    . = ALIGN(8);
    __StackStart = .;
    . += __stack_size__;
  } > DTCM

  __StackTop   = ORIGIN(DTCM) + LENGTH(DTCM);
  __StackLimit = __StackTop - __stack_size__;
  PROVIDE(__stack = __StackTop);
  ASSERT(__StackLimit >= __StackStart, "System TCM overflowed with data, heap and stack")

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
