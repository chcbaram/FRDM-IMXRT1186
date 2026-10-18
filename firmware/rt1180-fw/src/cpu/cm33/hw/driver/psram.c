#include "psram.h"


#ifdef _USE_HW_PSRAM
#include "cli.h"
#include "log.h"
#include "fsl_clock.h"
#include <string.h>

/*
 * PSRAM — HyperRAM W956A8MBYA 8 MB, FlexSPI1 포트 B (docs/27-hyperram.md)
 *
 * 설정은 BootROM 이 한다. 부트 헤더의 XMCD(0x0400_0800, bsp/boot/boot_hdr.c) 를 읽고
 * 핀 · 클럭 · LUT 를 잡아 0x2800_0000 에 메모리 맵으로 열어 둔다. 여기서는
 * ROM 이 열어 두었는지(FlexSPI1 MCR0.MDIS, FLSHB1CR0 크기)를 보고 짧게 써서 확인만 한다.
 *
 * 8 MB 를 두 코어가 나눠 갖는다 (SDK 와 같은 분할). 링커의 PSRAM 영역이 이 코어 몫이다.
 *   0x2800_0000  6 MB  CM7
 *   0x2860_0000  2 MB  CM33   ← 여기
 * 확인이 끝나면 .psram_bss 를 0 으로 채운다. 그 전에는 PSRAM_BSS 변수를 쓰면 안 된다.
 *
 * CM33 은 시스템 버스 캐시(XCACHE_PS) 가 꺼져 있어 HyperRAM 을 캐시하지 않는다.
 * CM7 은 MPU 영역 8 이 Normal write-back 으로 연다 (cm7/bsp/mpu.c).
 */

#define PSRAM_DEV_ADDR        0x28000000UL  // FlexSPI1 AHB 창 (NS). 두 코어 몫 전체
#define PSRAM_PORT_B1         2             // FLSHCR0[] : A1, A2, B1, B2


#if CLI_USE(HW_PSRAM)
static void cliPsram(cli_args_t *args);
#endif
static bool psramCheck(void);


extern uint32_t __psram_region_start[];
extern uint32_t __psram_region_size[];
extern uint32_t __psram_bss_start__[];
extern uint32_t __psram_bss_end__[];

static bool     is_init    = false;
static uint32_t dev_length = 0;             // 칩 전체 (ROM 이 감지한 크기)




bool psramInit(void)
{
  //-- ROM 이 FlexSPI1 을 켜고 포트 B1 크기(KB)를 적어 두었는지
  //
  if ((FLEXSPI1->MCR0 & FLEXSPI_MCR0_MDIS_MASK) == 0)
  {
    dev_length = (FLEXSPI1->FLSHCR0[PSRAM_PORT_B1] & FLEXSPI_FLSHCR0_FLSHSZ_MASK) * 1024U;
  }

  //-- 링커가 잡은 이 코어 몫이 칩 안에 들어야 한다
  //
  is_init = (dev_length > 0) &&
            ((uint32_t)__psram_region_start + (uint32_t)__psram_region_size <= PSRAM_DEV_ADDR + dev_length) &&
            psramCheck();

  if (is_init)
  {
    for (uint32_t *p = __psram_bss_start__; p < __psram_bss_end__; p++)
    {
      *p = 0;
    }
  }

  logPrintf("[%s] psramInit()\n", is_init ? "OK":"E_");
  if (is_init)
  {
    logPrintf("     HyperRAM  %d KB, CM33 %d KB @ 0x%08X (bss %d B)\n",
              (int)(dev_length / 1024), (int)(psramGetLength() / 1024), (unsigned)psramGetAddr(),
              (int)((uint32_t)__psram_bss_end__ - (uint32_t)__psram_bss_start__));
  }

#if CLI_USE(HW_PSRAM)
  cliAdd("psram", cliPsram);
#endif
  return is_init;
}

bool psramIsInit(void)
{
  return is_init;
}

uint32_t psramGetAddr(void)
{
  return (uint32_t)__psram_region_start;
}

uint32_t psramGetLength(void)
{
  return is_init ? (uint32_t)__psram_region_size : 0;
}

static bool psramCheck(void)
{
  //-- 이 코어 몫의 처음 · 가운데 · 끝 워드에 두 패턴을 써서 읽어 본다. 원래 값은 되돌려 둔다.
  //
  volatile uint32_t *p = (volatile uint32_t *)__psram_region_start;
  uint32_t           words = (uint32_t)__psram_region_size / 4;
  const uint32_t     idx[3] = {0, words / 2, words - 1};
  bool               ret = true;

  for (int i = 0; i < 3; i++)
  {
    uint32_t keep = p[idx[i]];

    p[idx[i]] = 0x5AA5F00FU;
    __DSB();
    ret &= (p[idx[i]] == 0x5AA5F00FU);
    p[idx[i]] = ~0x5AA5F00FU;
    __DSB();
    ret &= (p[idx[i]] == ~0x5AA5F00FU);
    p[idx[i]] = keep;
  }
  return ret;
}


#if CLI_USE(HW_PSRAM)
static uint32_t cycStart(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
  return DWT->CYCCNT;
}

static uint32_t cycToUs(uint32_t start)
{
  return (DWT->CYCCNT - start) / (SystemCoreClock / 1000000U);
}

void cliPsram(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("init      : %s\n", is_init ? "OK" : "E_");
    cliPrintf("device    : %d KB @ 0x%08X (FLSHB1CR0)\n", (int)(dev_length / 1024), (unsigned)PSRAM_DEV_ADDR);
    cliPrintf("CM33 몫   : %d KB @ 0x%08X\n", (int)((uint32_t)__psram_region_size / 1024), (unsigned)__psram_region_start);
    cliPrintf("bss       : 0x%08X ~ 0x%08X (%d B)\n", (unsigned)__psram_bss_start__, (unsigned)__psram_bss_end__,
              (int)((uint32_t)__psram_bss_end__ - (uint32_t)__psram_bss_start__));
    cliPrintf("FlexSPI1  : MCR0 0x%08X  root %d Hz\n", (unsigned)FLEXSPI1->MCR0,
              (int)CLOCK_GetRootClockFreq(kCLOCK_Root_Flexspi1));
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "test"))
  {
    //-- 이 코어 몫만 시험한다. CM7 몫(앞 6 MB)은 건드리지 않는다.
    //   bss 뒤부터 시험해 PSRAM_BSS 변수는 남긴다.
    volatile uint32_t *p      = (volatile uint32_t *)__psram_bss_end__;
    uint32_t           length = (uint32_t)__psram_region_start + (uint32_t)__psram_region_size - (uint32_t)__psram_bss_end__;
    uint32_t           words  = length / 4;
    uint32_t           blk    = (length / 2) & ~0xFFFU;
    uint32_t           bad, t, us;

    if (is_init != true)
    {
      cliPrintf("E_ not init\n");
      return;
    }

    //-- 1. 데이터 버스 : 첫 워드에 1 을 한 비트씩 옮겨 쓴다
    bad = 0;
    for (uint32_t b = 0; b < 32; b++)
    {
      p[0] = 1U << b;
      __DSB();
      bad += (p[0] != (1U << b));
    }
    cliPrintf("data bus (walking 1) : %s\n", bad == 0 ? "OK" : "E_");

    //-- 2. 주소 버스 : 2^n 워드 위치마다 다른 값. 주소선이 붙거나 끊기면 서로 덮어쓴다
    bad = 0;
    p[0] = 0xAAAAAAAAU;
    for (uint32_t off = 1; off < words; off <<= 1) p[off] = off;
    for (uint32_t off = 1; off < words; off <<= 1) bad += (p[off] != off);
    bad += (p[0] != 0xAAAAAAAAU);
    cliPrintf("addr bus (2^n)       : %s\n", bad == 0 ? "OK" : "E_");

    //-- 3. 전체 : 주소를 값으로 써서 비교, 그다음 반전으로 한 번 더
    t = cycStart();
    for (uint32_t i = 0; i < words; i++) p[i] = (uint32_t)&p[i];
    us = cycToUs(t);
    bad = 0;
    t = cycStart();
    for (uint32_t i = 0; i < words; i++) bad += (p[i] != (uint32_t)&p[i]);
    uint32_t us_rd = cycToUs(t);
    for (uint32_t i = 0; i < words; i++) p[i] = ~(uint32_t)&p[i];
    for (uint32_t i = 0; i < words; i++) bad += (p[i] != ~(uint32_t)&p[i]);
    cliPrintf("full %d KB (addr, ~addr) : %s  (%d words differ)\n", (int)(length / 1024), bad == 0 ? "OK" : "E_", (int)bad);
    cliPrintf("  write 32bit loop   : %7d us  (%d KB/s)\n", (int)us,    us    ? (int)((uint64_t)length * 1000000U / us / 1024U)    : 0);
    cliPrintf("  read  32bit loop   : %7d us  (%d KB/s)\n", (int)us_rd, us_rd ? (int)((uint64_t)length * 1000000U / us_rd / 1024U) : 0);

    //-- 4. memset / memcpy (시험 구간의 절반씩)
    t = cycStart();
    memset((void *)p, 0x55, blk);
    us = cycToUs(t);
    cliPrintf("  memset %4d KB     : %7d us  (%d KB/s)\n", (int)(blk / 1024), (int)us, us ? (int)((uint64_t)blk * 1000000U / us / 1024U) : 0);
    t = cycStart();
    memcpy((void *)((uint32_t)p + blk), (const void *)p, blk);
    us = cycToUs(t);
    cliPrintf("  memcpy %4d KB     : %7d us  (%d KB/s)\n", (int)(blk / 1024), (int)us, us ? (int)((uint64_t)blk * 1000000U / us / 1024U) : 0);
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("psram info\n");
    cliPrintf("psram test        (CM33 몫 2 MB 중 bss 뒤를 덮어쓴다)\n");
  }
}
#endif

#endif
