#include "bsp.h"

/*
 * CM33 MPU 와 시스템 버스 캐시(XCACHE_PS) — SDK frdmimxrt1186/board.c 의 BOARD_ConfigMPU() (CM33) 를 따른다
 *
 * XCACHE 는 MPU 속성(캐시 여부 · WB/WT)을 따른다 (RM 39). ARMv8-M MPU 는 영역이 겹치면
 * fault 이므로 겹치지 않게 나눠 적는다. 적지 않은 곳은 기본 맵(PRIVDEFENA)을 따른다.
 * 0x2000_0000 ~ 0x3FFF_FFFF 의 기본 속성은 Normal WB 라서, 캐시하면 안 되는 곳은 꼭 적는다.
 *
 *   0  0x0FFE_0000  128 KB  Normal WT        Code TCM
 *   1  0x2000_0000  128 KB  Normal WB        System TCM
 *   2  0x203C_0000  512 KB  non-cacheable XN CM7 TCM (NS alias)
 *   3  0x303C_0000  512 KB  non-cacheable XN CM7 TCM (S alias) — cm7Load() 가 이미지를 쓴다
 *   4  0x0400_0000  16 MB   Normal WT, RO    QSPI (XIP). ROM API 는 IP 명령으로 쓰므로 RO 와 무관
 *   5  0x4000_0000  1 GB    Device XN        주변장치
 *   6  0x2048_0000  512 KB  non-cacheable    OCRAM1 — RM 39: ECC OCRAM 은 non-cacheable 또는 WT
 *   7  0x2050_0000  256 KB  non-cacheable    OCRAM2 — 공유 블록이 있다 (shared.h)
 *   8  0x2800_0000  6 MB    non-cacheable    HyperRAM CM7 몫 — 상대 코어 영역은 캐시하지 않는다
 *   9  0x2860_0000  2 MB    Normal WB        HyperRAM CM33 몫 (.psram_bss, docs/27)
 *
 * TCM 은 XCACHE 를 거치지 않는다. 0 · 1 번은 SDK 와 같이 속성만 준다.
 * Secure alias(0x3048_0000, 0x3800_0000 …)는 적지 않았다. 쓰지 않는다.
 */

#define ATTR_DEVICE     0
#define ATTR_NC         1
#define ATTR_WT         2
#define ATTR_WB         3

#define HYPERRAM_ADDR   0x28000000UL


extern uint32_t __psram_region_start[];
extern uint32_t __psram_region_size[];




void mpuInit(void)
{
  uint32_t psram_start = (uint32_t)__psram_region_start;
  uint32_t psram_end   = psram_start + (uint32_t)__psram_region_size - 1;

  ARM_MPU_Disable();

  ARM_MPU_SetMemAttr(ATTR_DEVICE, ARM_MPU_ATTR(ARM_MPU_ATTR_DEVICE, ARM_MPU_ATTR_DEVICE));
  ARM_MPU_SetMemAttr(ATTR_NC,     ARM_MPU_ATTR(ARM_MPU_ATTR_NON_CACHEABLE, ARM_MPU_ATTR_NON_CACHEABLE));
  ARM_MPU_SetMemAttr(ATTR_WT,     ARM_MPU_ATTR(ARM_MPU_ATTR_MEMORY_(0U, 0U, 1U, 0U), ARM_MPU_ATTR_MEMORY_(0U, 0U, 1U, 0U)));
  ARM_MPU_SetMemAttr(ATTR_WB,     ARM_MPU_ATTR(ARM_MPU_ATTR_MEMORY_(0U, 1U, 1U, 1U), ARM_MPU_ATTR_MEMORY_(0U, 1U, 1U, 1U)));

  //--                     RBAR(base, shareable, RO, non-privileged, XN)        RLAR(limit, attr)
  ARM_MPU_SetRegion(0U, ARM_MPU_RBAR(0x0FFE0000U, ARM_MPU_SH_NON, 0U, 1U, 0U), ARM_MPU_RLAR(0x0FFFFFFFU, ATTR_WT));
  ARM_MPU_SetRegion(1U, ARM_MPU_RBAR(0x20000000U, ARM_MPU_SH_NON, 0U, 1U, 0U), ARM_MPU_RLAR(0x2001FFFFU, ATTR_WB));
  ARM_MPU_SetRegion(2U, ARM_MPU_RBAR(0x203C0000U, ARM_MPU_SH_NON, 0U, 1U, 1U), ARM_MPU_RLAR(0x2043FFFFU, ATTR_NC));
  ARM_MPU_SetRegion(3U, ARM_MPU_RBAR(0x303C0000U, ARM_MPU_SH_NON, 0U, 1U, 1U), ARM_MPU_RLAR(0x3043FFFFU, ATTR_NC));
  ARM_MPU_SetRegion(4U, ARM_MPU_RBAR(0x04000000U, ARM_MPU_SH_NON, 1U, 1U, 0U), ARM_MPU_RLAR(0x04FFFFFFU, ATTR_WT));
  ARM_MPU_SetRegion(5U, ARM_MPU_RBAR(0x40000000U, ARM_MPU_SH_NON, 0U, 1U, 1U), ARM_MPU_RLAR(0x7FFFFFFFU, ATTR_DEVICE));
  ARM_MPU_SetRegion(6U, ARM_MPU_RBAR(0x20480000U, ARM_MPU_SH_NON, 0U, 1U, 0U), ARM_MPU_RLAR(0x204FFFFFU, ATTR_NC));
  ARM_MPU_SetRegion(7U, ARM_MPU_RBAR(0x20500000U, ARM_MPU_SH_NON, 0U, 1U, 0U), ARM_MPU_RLAR(0x2053FFFFU, ATTR_NC));
  if (psram_start > HYPERRAM_ADDR)
  {
    ARM_MPU_SetRegion(8U, ARM_MPU_RBAR(HYPERRAM_ADDR, ARM_MPU_SH_NON, 0U, 1U, 0U), ARM_MPU_RLAR(psram_start - 1, ATTR_NC));
  }
  ARM_MPU_SetRegion(9U, ARM_MPU_RBAR(psram_start, ARM_MPU_SH_NON, 0U, 1U, 0U), ARM_MPU_RLAR(psram_end, ATTR_WB));

  ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_HFNMIENA_Msk);

  //-- 시스템 버스 캐시를 무효화하고 켠다 (system_MIMXRT1186_cm33.c 가 XCACHE_PC 를 켜는 방법과 같다)
  //
  if ((XCACHE_PS->CCR & XCACHE_CCR_ENCACHE_MASK) == 0U)
  {
    XCACHE_PS->CCR = XCACHE_CCR_INVW1_MASK | XCACHE_CCR_INVW0_MASK;
    XCACHE_PS->CCR |= XCACHE_CCR_GO_MASK;
    while ((XCACHE_PS->CCR & XCACHE_CCR_GO_MASK) != 0U)
    {
    }
    XCACHE_PS->CCR = XCACHE_CCR_ENCACHE_MASK;
  }
  __DSB();
  __ISB();
}
