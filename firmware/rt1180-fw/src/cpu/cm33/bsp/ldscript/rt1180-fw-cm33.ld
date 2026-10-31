/*
 * rt1180-fw CM33 (앱) — FlexSPI2 QSPI NOR 에서 XIP 로 실행, 부트로더가 점프해 들어온다
 *
 * 근거: docs/40-boot-architecture.md 2절, docs/42-app-jump.md
 * SDK 의 MIMXRT1186xxxxx_cm33_flexspi_nor.ld 를 바탕으로 쓰지 않는 영역
 * (rpmsg, core1 이미지, non-cacheable)을 걷어냈다. HyperRAM 은 SDK 처럼 .data 를 옮기지 않고
 * .psram_bss 로 큰 버퍼만 둔다.
 *
 *   0x0400_0000  부트 슬롯 256 KB  부트 헤더 + 부트로더 (rt1180-boot, 이 이미지에는 없다)
 *   0x0404_0000  TAG 4 KB          firm_tag_t (tools/mkfwtag.py 가 붙인다)
 *   0x0404_1000  벡터 테이블 1 KB   부트로더가 VTOR · MSP · PC 를 여기서 읽는다
 *   0x0404_1400  firm_ver_t        .version (hw.c)
 *                코드 → .data 초기값 → .ram_function
 *   0x0480_0000  CM7 슬롯 (넘으면 링크 단계에서 멈춘다)
 *   0x2860_0000  HyperRAM 뒤 2 MB → .psram_bss (CM7 몫은 앞 6 MB, docs/27)
 *
 * startup_MIMXRT1186_cm33.S 와 newlib crt0 가 쓰는 심볼 이름은 SDK 와 같게 둔다.
 */

ENTRY(Reset_Handler)

__stack_size__ = DEFINED(__stack_size__) ? __stack_size__ : 0x2000;
__heap_size__  = DEFINED(__heap_size__)  ? __heap_size__  : 0x1000;

firm_start       = 0x04041000;    /* TAG(0x0404_0000) 바로 뒤 */
firm_end         = 0x04800000;    /* CM7 슬롯 */
vector_size      = 0x00000400;    /* 16 + 240 엔트리 */

MEMORY
{
  VECTORS    (RX)  : ORIGIN = firm_start, LENGTH = vector_size
  FLASH      (RX)  : ORIGIN = firm_start + vector_size, LENGTH = firm_end - firm_start - vector_size

  ITCM       (RWX) : ORIGIN = 0x0FFE0000, LENGTH = 128K    /* CM33 Code TCM   */
  DTCM       (RW)  : ORIGIN = 0x20000000, LENGTH = 128K    /* CM33 System TCM */
  SHARED     (RW)  : ORIGIN = 0x20500000, LENGTH = 4K      /* OCRAM2 앞 4 KB : 코어 간 공유 블록 (cpu/shared/shared.h) */
  PSRAM      (RW)  : ORIGIN = 0x28600000, LENGTH = 2M      /* HyperRAM 뒤 2 MB : CM33 몫 (SDK 와 같은 분할) */
}

/* firm_ver_t 가 쓰는 값 (hw/hw.c) */
_fw_flash_begin = ORIGIN(VECTORS);

SECTIONS
{
  .interrupts :
  {
    . = ALIGN(4);
    __Vectors = .;
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > VECTORS

  /* 벡터 바로 뒤 (+0x400). 부트로더와 PC 도구가 이 자리에서 읽는다 */
  .version :
  {
    KEEP(*(.version))
  } > FLASH

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

  /* 이미지 = 벡터부터 마지막 로드 바이트까지. TAG 의 CRC 범위와 firm_ver_t.firm_size */
  __image_end    = __ram_function_flash_start + (__ram_function_end__ - __ram_function_start__);
  _fw_flash_size = __image_end - _fw_flash_begin;
  ASSERT(__image_end <= ORIGIN(FLASH) + LENGTH(FLASH), "image runs into the CM7 slot")

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

  /* 코어 간 공유 블록. NOLOAD 라 이미지에 담기지 않고 시작 시 0 으로 밀리지 않는다.
     CM7 링커 스크립트도 같은 주소에 같은 섹션을 놓는다. */
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
