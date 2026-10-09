/*
 * rt1180-fw CM33 — FlexSPI2 QSPI NOR 에서 XIP 로 실행
 *
 * 근거: docs/01-boot-sequence.md 4절, docs/02-memory-map.md 4~5절
 * SDK 의 MIMXRT1186xxxxx_cm33_flexspi_nor.ld 를 바탕으로 쓰지 않는 영역
 * (HyperRAM, rpmsg, core1 이미지, non-cacheable)을 걷어냈다.
 *
 *   0x0400_0400  FCB        .boot_hdr.conf       ROM 이 FlexSPI 설정을 읽는다
 *   0x0400_0800  XMCD       .boot_hdr.xmcd_data  (비움)
 *   0x0400_1000  컨테이너   .boot_hdr.container  ROM/ELE 가 이미지 위치를 읽는다
 *   0x0400_B000  벡터 테이블 → 코드 → .data 초기값
 *
 * 이미지는 파티션 계획의 첫 256 KB(부트로더 자리) 안에 있어야 한다.
 * 넘치면 링크 단계에서 멈춘다.
 *
 * startup_MIMXRT1186_cm33.S 와 newlib crt0 가 쓰는 심볼 이름은 SDK 와 같게 둔다.
 */

ENTRY(Reset_Handler)

__stack_size__ = DEFINED(__stack_size__) ? __stack_size__ : 0x2000;
__heap_size__  = DEFINED(__heap_size__)  ? __heap_size__  : 0x1000;

flash_start      = 0x04000000;
slot_size        = 0x00040000;    /* 256 KB : 부트 헤더 + 이 이미지 */
image_offset     = 0x0000B000;    /* SDK 와 같은 위치 */
vector_size      = 0x00000400;    /* 16 + 240 엔트리 */

MEMORY
{
  FCB        (RX)  : ORIGIN = flash_start + 0x0400, LENGTH = 0x200
  XMCD       (RX)  : ORIGIN = flash_start + 0x0800, LENGTH = 0x200
  CONTAINER  (RX)  : ORIGIN = flash_start + 0x1000, LENGTH = 0x2000
  VECTORS    (RX)  : ORIGIN = flash_start + image_offset, LENGTH = vector_size
  FLASH      (RX)  : ORIGIN = flash_start + image_offset + vector_size,
                     LENGTH = slot_size - image_offset - vector_size

  ITCM       (RWX) : ORIGIN = 0x0FFE0000, LENGTH = 128K    /* CM33 Code TCM   */
  DTCM       (RW)  : ORIGIN = 0x20000000, LENGTH = 128K    /* CM33 System TCM */
}

/* 컨테이너 이미지 엔트리가 쓰는 값 (bsp/boot/boot_hdr.c) */
__CONTAINER_IMG_OFFSET = ORIGIN(VECTORS) - ORIGIN(CONTAINER);
__VECTOR_TABLE         = ORIGIN(VECTORS);

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
  } > VECTORS

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
  } > FLASH

  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } > FLASH

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } > FLASH

  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } > FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } > FLASH

  /* 모듈 등록 테이블 (titan-mini 와 같은 MODULE_DEF 구조, 로드맵 29) */
  .module :
  {
    . = ALIGN(4);
    _smodule = .;
    KEEP (*(.module))
    KEEP (*(.module*))
    _emodule = .;
  } > FLASH

  __etext = .;

  /* .data : 플래시에 초기값, DTCM 에서 실행 */
  __data_flash_start = .;
  .data : AT(__data_flash_start)
  {
    . = ALIGN(4);
    __data_start__ = .;
    *(.data)
    *(.data*)
    *(DataQuickAccess)
    . = ALIGN(4);
    __data_end__ = .;
  } > DTCM

  /* 빠른 코드 : 플래시에 원본, ITCM 으로 복사 (__STARTUP_INITIALIZE_RAMFUNCTION) */
  __ram_function_flash_start = __data_flash_start + (__data_end__ - __data_start__);
  .ram_function : AT(__ram_function_flash_start)
  {
    . = ALIGN(32);
    __ram_function_start__ = .;
    *(CodeQuickAccess)
    *(.ramfunc*)
    . = ALIGN(128);
    __ram_function_end__ = .;
  } > ITCM

  /* ROM 이 복사/검증할 이미지 크기 = 벡터부터 마지막 로드 바이트까지 */
  __image_end = __ram_function_flash_start + (__ram_function_end__ - __ram_function_start__);
  __CONTAINER_IMG_SIZE = __image_end - ORIGIN(VECTORS);
  ASSERT(__image_end <= ORIGIN(FLASH) + LENGTH(FLASH), "image exceeds the 256 KB boot slot")

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

  /* 스택은 DTCM 끝에 붙인다. MSPLIM 은 __StackLimit 으로 걸린다. */
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

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
