#include "bsp.h"
#include "shared.h"

/*
 * CM7 MPU 와 L1 캐시 — SDK frdmimxrt1186/board.c 의 BOARD_EarlyConfigMPU() 를 줄인 것
 *
 * SystemInit() 이 .data 복사와 .bss 클리어 **전에** BOARD_EarlyInit() 을 부른다
 * (system_MIMXRT1186_cm7.c). 그래서 여기서는 전역/정적 변수를 쓰면 안 된다.
 *
 * 영역은 번호가 클수록 우선한다. 주소는 CM7 이 보는 주소다 (docs/02-memory-map.md 1절).
 *
 *   0  0x0000_0000  4 GB    접근 금지, XN   Arm 에라타 1013783 (추측 접근) 회피. 아래에서 필요한 곳만 연다
 *   1  0x8000_0000  512 MB  Device, XN      SEMC
 *   2  0x0000_0000  2 GB    Device, XN      주변장치 0x4000_0000~ 포함. 메모리는 아래에서 덮는다
 *   3  0x0000_0000  256 KB  Normal WB       ITCM (TCM 은 L1 캐시를 거치지 않는다. 속성만 Normal)
 *   4  0x2000_0000  256 KB  Normal WB       DTCM
 *   5  0x2048_0000  512 KB  Normal WB       OCRAM1
 *   6  0x2050_0000  256 KB  Normal WB       OCRAM2
 *   7  0x2050_0000  4 KB    Normal NC       공유 블록. CM33 과 같은 값을 봐야 하므로 캐시하지 않는다
 *   8  0x2800_0000  8 MB    Normal WB       HyperRAM (FlexSPI1, ROM 이 XMCD 로 설정, docs/27)
 *
 * QSPI(0x0400_0000)는 CM7 이 쓰지 않아 열지 않았다(영역 2 의 Device 로 남는다).
 * HyperRAM 은 CM7 이 캐시한다. CM33 과 같은 구간을 주고받으려면 캐시를 비우고 무효화해야 한다.
 */

static void mpuConfig(void);


void BOARD_EarlyInit(void)
{
  //-- MPU 를 바꾸는 동안 캐시는 꺼 둔다. 기동 직후라 비어 있지만 다시 들어와도 안전하게.
  //
  SCB_DisableICache();
  SCB_DisableDCache();

  mpuConfig();

  //-- SCB_Enable*Cache 는 무효화한 뒤 켠다 (CMSIS armv7m_cachel1.h)
  //
  SCB_EnableDCache();
  SCB_EnableICache();
}

static void mpuConfig(void)
{
  ARM_MPU_Disable();

  MPU->RBAR = ARM_MPU_RBAR(0, 0x00000000U);
  MPU->RASR = ARM_MPU_RASR(1, ARM_MPU_AP_NONE, 0, 0, 0, 0, 0, ARM_MPU_REGION_SIZE_4GB);

  MPU->RBAR = ARM_MPU_RBAR(1, 0x80000000U);
  MPU->RASR = ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 2, 0, 0, 0, 0, ARM_MPU_REGION_SIZE_512MB);

  MPU->RBAR = ARM_MPU_RBAR(2, 0x00000000U);
  MPU->RASR = ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 2, 0, 0, 0, 0, ARM_MPU_REGION_SIZE_2GB);

  MPU->RBAR = ARM_MPU_RBAR(3, 0x00000000U);
  MPU->RASR = ARM_MPU_RASR(0, ARM_MPU_AP_FULL, 0, 0, 1, 1, 0, ARM_MPU_REGION_SIZE_256KB);

  MPU->RBAR = ARM_MPU_RBAR(4, 0x20000000U);
  MPU->RASR = ARM_MPU_RASR(0, ARM_MPU_AP_FULL, 0, 0, 1, 1, 0, ARM_MPU_REGION_SIZE_256KB);

  MPU->RBAR = ARM_MPU_RBAR(5, 0x20480000U);
  MPU->RASR = ARM_MPU_RASR(0, ARM_MPU_AP_FULL, 0, 0, 1, 1, 0, ARM_MPU_REGION_SIZE_512KB);

  MPU->RBAR = ARM_MPU_RBAR(6, 0x20500000U);
  MPU->RASR = ARM_MPU_RASR(0, ARM_MPU_AP_FULL, 0, 0, 1, 1, 0, ARM_MPU_REGION_SIZE_256KB);

  //-- TEX=1, C=0, B=0 : Normal, non-cacheable
  //
  _Static_assert(SHARED_BLOCK_SIZE == 0x1000U, "MPU region 7 size is 4 KB");
  _Static_assert((SHARED_BLOCK_ADDR % SHARED_BLOCK_SIZE) == 0, "MPU region base must be size aligned");
  MPU->RBAR = ARM_MPU_RBAR(7, SHARED_BLOCK_ADDR);
  MPU->RASR = ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 1, 0, 0, 0, 0, ARM_MPU_REGION_SIZE_4KB);

  MPU->RBAR = ARM_MPU_RBAR(8, 0x28000000U);
  MPU->RASR = ARM_MPU_RASR(0, ARM_MPU_AP_FULL, 0, 0, 1, 1, 0, ARM_MPU_REGION_SIZE_8MB);

  ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_HFNMIENA_Msk);
}
