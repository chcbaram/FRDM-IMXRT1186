#include "cm7.h"
#include "bsp.h"
#include "fsl_clock.h"
#include "fsl_dcdc.h"
#include "fsl_pmu.h"
#include "shared.h"

/*
 * CM7 기동 — docs/25-cm7-boot.md
 *
 * CM7 은 리셋이 묶인 채로 시작한다(BOOT_CFG7 RELEASE_M7_RST_STAT=0). 퓨즈를 굽지
 * 않고 CM33 이 깨운다. 순서는 SDK 멀티코어 예제(MCMGR, imxrt1180) 와 같다.
 *
 *   1. cm7Prepare()
 *      - Prepare_CM7(0) (system_MIMXRT1186_cm33.c)
 *          ARM PLL 켜기, INITVTOR=0, SRC BT_RELEASE_M7 (리셋 해제, 아직 WAIT),
 *          DMA4 로 CM7 ITCM/DTCM 512 KB 를 64 bit 쓰기로 채워 ECC 초기화
 *      - 오버드라이브 : DCDC CORE0/1 1.125 V, FBB 켜기 (SDK BOARD_BootClockRUN 과 같다)
 *      - ARM PLL 792 MHz 를 다시 잡고(락 대기) M7 루트를 ARM PLL 로
 *   2. cm7Load()   : 이미지를 0x303C_0000 (CM33 에서 본 CM7 ITCM) 에 복사
 *   3. cm7Start()  : ELE 에 CM7 kick-off 명령 → M7_CFG.WAIT 해제
 *
 * 3 이 핵심이다. CM7 은 레지스터만으로는 시작되지 않는다. EdgeLock Enclave(ELE)에
 * S3MU 메일박스로 명령 0x17D2_0106 을 보내고 응답(0xE1D2_0206, 0xD6)을 받아야 한다.
 */

#define ELE_CMD_KICK_CM7      0x17D20106UL      // tag 0x17, cmd 0xD2, size 1, ver 6
#define ELE_RESP_HDR          0xE1D20206UL
#define ELE_RESP_SUCCESS      0xD6U
#define ELE_POLL_MAX          10000000UL


static uint32_t ele_resp[2] = {0, 0};

static const clock_arm_pll_config_t arm_pll_cfg =
{
  .postDivider = kCLOCK_PllPostDiv2,
  .loopDivider = 132,                           // 24 MHz * 132 / (2 * 2) = 792 MHz
};




void cm7Prepare(void)
{
  clock_root_config_t cfg = {0};

  Prepare_CM7(0);

  //-- 오버드라이브. 클럭을 올리기 전에 전압부터 올린다.
  //
  //   SDK board.c 의 DCDC_SetVoltage() 는 이 호출을 ELE VOLTAGE_CHANGE_START/FINISH 로
  //   감싼다. GDET(전압 글리치 검출)를 쓰는 특수 ELE 펌웨어에서만 필요하고, 일반 ELE ROM
  //   은 그 명령을 오류로 응답하고 무시한다(SDK 주석). GDET 를 쓰지 않으므로 바로 쓴다.
  //
  DCDC_SetVDD1P0BuckModeTargetVoltage(DCDC, kDCDC_CORE0, kDCDC_1P0Target1P125V);
  DCDC_SetVDD1P0BuckModeTargetVoltage(DCDC, kDCDC_CORE1, kDCDC_1P0Target1P125V);
  PMU_EnableFBB(ANADIG_PMU, true);

  CLOCK_InitArmPll(&arm_pll_cfg);

  cfg.mux = kCLOCK_M7_ClockRoot_MuxArmPllOut;
  cfg.div = 1;
  CLOCK_SetRootClock(kCLOCK_Root_M7, &cfg);
}

void cm7Load(const void *p_src, uint32_t size)
{
  //-- TCM 은 Prepare_CM7 이 64 bit 쓰기로 ECC 를 초기화해 두었으므로 32 bit 복사가 된다
  //
  volatile uint32_t *p_dst = (volatile uint32_t *)SHARED_CM7_TCM_ADDR;
  const uint32_t    *p_s   = (const uint32_t *)p_src;

  for (uint32_t i = 0; i < (size + 3) / 4; i++)
  {
    p_dst[i] = p_s[i];
  }
  __DSB();
}

cm7_err_t cm7Start(void)
{
  uint32_t cnt;

  //-- ELE 에 CM7 kick-off 요청
  //
  for (cnt = 0; (MU_RT_S3MUA->TSR & (1U << 0)) == 0; cnt++)       // TE0 : 송신 레지스터 빔
  {
    if (cnt >= ELE_POLL_MAX) return CM7_ERR_ELE_BUSY;
  }
  MU_RT_S3MUA->TR[0] = ELE_CMD_KICK_CM7;

  for (cnt = 0; (MU_RT_S3MUA->RSR & (1U << 0)) == 0; cnt++)       // RF0
  {
    if (cnt >= ELE_POLL_MAX) return CM7_ERR_ELE_TIMEOUT;
  }
  for (cnt = 0; (MU_RT_S3MUA->RSR & (1U << 1)) == 0; cnt++)       // RF1
  {
    if (cnt >= ELE_POLL_MAX) return CM7_ERR_ELE_TIMEOUT;
  }

  //-- 응답은 반드시 읽어야 다음 명령을 받는다
  //
  ele_resp[0] = MU_RT_S3MUA->RR[0];
  ele_resp[1] = MU_RT_S3MUA->RR[1];

  if (ele_resp[0] != ELE_RESP_HDR || (ele_resp[1] & 0xFFU) != ELE_RESP_SUCCESS)
  {
    return CM7_ERR_ELE_RESP;
  }

  //-- WAIT 해제. MCMGR 처럼 M7 클럭을 잠깐 끄고 바꾼다.
  //
  CLOCK_DisableClock(kCLOCK_M7);
  BLK_CTRL_S_AONMIX->M7_CFG = (BLK_CTRL_S_AONMIX->M7_CFG & ~BLK_CTRL_S_AONMIX_M7_CFG_WAIT_MASK) |
                              BLK_CTRL_S_AONMIX_M7_CFG_WAIT(0);
  CLOCK_EnableClock(kCLOCK_M7);

  return CM7_OK;
}

uint32_t cm7GetEleResp(uint32_t index)
{
  return (index < 2) ? ele_resp[index] : 0;
}
