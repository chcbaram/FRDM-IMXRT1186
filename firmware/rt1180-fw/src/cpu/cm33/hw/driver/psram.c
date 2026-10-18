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
 * CM33 은 시스템 버스 캐시(XCACHE_PS) 가 꺼져 있어 HyperRAM 을 캐시하지 않는다.
 * CM7 은 MPU 영역 8 이 Normal write-back 으로 연다 (cm7/bsp/mpu.c).
 */

#define PSRAM_ADDR            0x28000000UL  // FlexSPI1 AHB 창 (NS)
#define PSRAM_PORT_B1         2             // FLSHCR0[] : A1, A2, B1, B2


#if CLI_USE(HW_PSRAM)
static void cliPsram(cli_args_t *args);
#endif
static bool psramCheck(void);


static bool     is_init = false;
static uint32_t length  = 0;




bool psramInit(void)
{
  //-- ROM 이 FlexSPI1 을 켜고 포트 B1 크기(KB)를 적어 두었는지
  //
  if ((FLEXSPI1->MCR0 & FLEXSPI_MCR0_MDIS_MASK) == 0)
  {
    length = (FLEXSPI1->FLSHCR0[PSRAM_PORT_B1] & FLEXSPI_FLSHCR0_FLSHSZ_MASK) * 1024U;
  }

  is_init = (length > 0) && psramCheck();

  logPrintf("[%s] psramInit()\n", is_init ? "OK":"E_");
  if (is_init)
  {
    logPrintf("     HyperRAM  %d KB @ 0x%08X (XMCD)\n", (int)(length / 1024), (unsigned)PSRAM_ADDR);
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
  return PSRAM_ADDR;
}

uint32_t psramGetLength(void)
{
  return is_init ? length : 0;
}

static bool psramCheck(void)
{
  //-- 처음 · 가운데 · 끝 워드에 두 패턴을 써서 읽어 본다. 원래 값은 되돌려 둔다.
  //
  volatile uint32_t *p = (volatile uint32_t *)PSRAM_ADDR;
  const uint32_t     idx[3] = {0, length / 8, length / 4 - 1};
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
    cliPrintf("addr      : 0x%08X\n", (unsigned)PSRAM_ADDR);
    cliPrintf("size      : %d KB (FLSHB1CR0)\n", (int)(length / 1024));
    cliPrintf("FlexSPI1  : MCR0 0x%08X  root %d Hz\n", (unsigned)FLEXSPI1->MCR0,
              (int)CLOCK_GetRootClockFreq(kCLOCK_Root_Flexspi1));
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "test"))
  {
    volatile uint32_t *p     = (volatile uint32_t *)PSRAM_ADDR;
    uint32_t           words = length / 4;
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

    //-- 4. memset / memcpy 1 MB
    t = cycStart();
    memset((void *)PSRAM_ADDR, 0x55, 1024 * 1024);
    us = cycToUs(t);
    cliPrintf("  memset 1 MB        : %7d us  (%d KB/s)\n", (int)us, us ? (int)(1024U * 1000000U / us) : 0);
    t = cycStart();
    memcpy((void *)(PSRAM_ADDR + 1024 * 1024), (const void *)PSRAM_ADDR, 1024 * 1024);
    us = cycToUs(t);
    cliPrintf("  memcpy 1 MB        : %7d us  (%d KB/s)\n", (int)us, us ? (int)(1024U * 1000000U / us) : 0);
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("psram info\n");
    cliPrintf("psram test        (8 MB 전체를 덮어쓴다)\n");
  }
}
#endif

#endif
