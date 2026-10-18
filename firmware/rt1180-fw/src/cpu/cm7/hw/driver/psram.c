#include "psram.h"


#ifdef _USE_HW_PSRAM

/*
 * PSRAM — CM7 쪽 (docs/27-hyperram.md)
 *
 * HyperRAM 은 BootROM 이 XMCD 로 열어 두었다. CM7 은 링커 PSRAM 영역(앞 6 MB)을 쓴다.
 * CM33 쪽과 같이 FlexSPI1 이 켜졌는지만 보고 .psram_bss 를 0 으로 채운다.
 * CM7 은 이 구간을 캐시한다 (cm7/bsp/mpu.c 영역 8). 0 채우기도 캐시를 거쳐 나간다.
 */

#define PSRAM_PORT_B1         2             // FLSHCR0[] : A1, A2, B1, B2


extern uint32_t __psram_region_start[];
extern uint32_t __psram_region_size[];
extern uint32_t __psram_bss_start__[];
extern uint32_t __psram_bss_end__[];

static bool is_init = false;




bool psramInit(void)
{
  uint32_t dev_length = 0;

  if ((FLEXSPI1->MCR0 & FLEXSPI_MCR0_MDIS_MASK) == 0)
  {
    dev_length = (FLEXSPI1->FLSHCR0[PSRAM_PORT_B1] & FLEXSPI_FLSHCR0_FLSHSZ_MASK) * 1024U;
  }

  is_init = (dev_length > 0);
  if (is_init)
  {
    for (uint32_t *p = __psram_bss_start__; p < __psram_bss_end__; p++)
    {
      *p = 0;
    }
  }
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

#endif
