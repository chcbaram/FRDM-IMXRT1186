#include "clock.h"
#include "bsp.h"
#include "fsl_clock.h"

/*
 * 클럭 설정 — docs/22-clock.md
 *
 * BootROM 은 BOOT_FREQ=0 이라 CM33 을 RCOSC200M 200 MHz 로 넘겨준다. 그런데
 * FlexSPI2 클럭 때문에 SYS_PLL3(480 MHz)은 이미 켜 두었다. 그래서 PLL 은 다시
 * 설정하지 않고 **클럭 루트만** 옮긴다. 실행 중인 XIP 의 클럭원(PLL3_PFD2)을
 * 건드리지 않으므로 FlexSPI 를 RAM 에서 다시 설정할 필요가 없다.
 *
 *   M33        RCOSC200M 200 MHz  →  SYS_PLL3 / 2 = 240 MHz   (Normal Drive 최대)
 *   BUS_AON    ROM 설정            →  SYS_PLL2 / 4 = 132 MHz
 *   BUS_WAKEUP ROM 설정            →  SYS_PLL2 / 4 = 132 MHz
 *   WAKEUP_AXI ROM 설정            →  SYS_PLL3 / 2 = 240 MHz
 *
 * 값은 SDK BOARD_BootClockRUN() 과 같다. PLL 이 꺼져 있으면 아무것도 바꾸지 않는다.
 * CM7 의 ARM PLL 800 MHz 와 오버드라이브 전압은 로드맵 25 에서 한다.
 */

#define CLOCK_APPLY      1     // 0 이면 ROM 설정을 그대로 둔다 (비교용)


typedef struct
{
  const char  *name;
  clock_root_t root;
  uint32_t     obs;           // CCM_OBS_xxx, 0 = 측정 안 함
} clock_root_tbl_t;

static const clock_root_tbl_t root_tbl[] =
{
  {"M7",         kCLOCK_Root_M7,         CCM_OBS_M7_CLK_ROOT},
  {"M33",        kCLOCK_Root_M33,        CCM_OBS_M33_CLK_ROOT},
  {"EDGELOCK",   kCLOCK_Root_Edgelock,   CCM_OBS_EDGELOCK_CLK_ROOT},
  {"BUS_AON",    kCLOCK_Root_Bus_Aon,    CCM_OBS_BUS_AON_CLK_ROOT},
  {"BUS_WAKEUP", kCLOCK_Root_Bus_Wakeup, CCM_OBS_BUS_WAKEUP_CLK_ROOT},
  {"WAKEUP_AXI", kCLOCK_Root_Wakeup_Axi, CCM_OBS_WAKEUP_AXI_CLK_ROOT},
  {"FLEXSPI2",   kCLOCK_Root_Flexspi2,   CCM_OBS_FLEXSPI2_CLK_ROOT},
  {"LPUART0102", kCLOCK_Root_Lpuart0102, CCM_OBS_LPUART0102_CLK_ROOT},
};

typedef struct
{
  const char *name;
  clock_pll_t pll;
} clock_pll_tbl_t;

static const clock_pll_tbl_t pll_tbl[] =
{
  {"ARM_PLL",  kCLOCK_PllArm},
  {"SYS_PLL1", kCLOCK_PllSys1},
  {"SYS_PLL2", kCLOCK_PllSys2},
  {"SYS_PLL3", kCLOCK_PllSys3},
  {"AUDIO",    kCLOCK_PllAudio},
};

static bool is_applied = false;


#if CLOCK_APPLY
static bool pllReady(clock_pll_t pll);
#endif




bool clockInit(void)
{
  is_applied = false;

#if CLOCK_APPLY
  clock_root_config_t cfg = {0};

  //-- 24 MHz 크리스탈. 이후 PLL 을 다시 잡을 때의 기준이다. 켜는 것만 하고
  //   이미 켜져 있으면 그대로 둔다. (SDK BOARD_BootClockRUN 과 같은 비트)
  //
  if ((ANADIG_OSC->OSC_24M_CTRL & ANADIG_OSC_OSC_24M_CTRL_OSC_24M_STABLE_MASK) == 0)
  {
    ANADIG_OSC->OSC_24M_CTRL |= ANADIG_OSC_OSC_24M_CTRL_OSC_EN(1) | ANADIG_OSC_OSC_24M_CTRL_LP_EN(1);
    while ((ANADIG_OSC->OSC_24M_CTRL & ANADIG_OSC_OSC_24M_CTRL_OSC_24M_STABLE_MASK) == 0)
    {
    }
  }

  //-- ROM 이 켠 PLL 을 쓴다. 없으면 바꾸지 않는다.
  //
  if (!pllReady(kCLOCK_PllSys3) || !pllReady(kCLOCK_PllSys2))
  {
    return false;
  }

  //-- 버스를 먼저 올리고 코어를 올린다.
  //   CCM 클럭 루트의 mux/div 변경은 하드웨어가 글리치 없이 처리한다.
  //
  cfg.mux = kCLOCK_BUS_AON_ClockRoot_MuxSysPll2Out;
  cfg.div = 4;
  CLOCK_SetRootClock(kCLOCK_Root_Bus_Aon, &cfg);

  cfg.mux = kCLOCK_BUS_WAKEUP_ClockRoot_MuxSysPll2Out;
  cfg.div = 4;
  CLOCK_SetRootClock(kCLOCK_Root_Bus_Wakeup, &cfg);

  cfg.mux = kCLOCK_WAKEUP_AXI_ClockRoot_MuxSysPll3Out;
  cfg.div = 2;
  CLOCK_SetRootClock(kCLOCK_Root_Wakeup_Axi, &cfg);

  cfg.mux = kCLOCK_M33_ClockRoot_MuxSysPll3Out;
  cfg.div = 2;
  CLOCK_SetRootClock(kCLOCK_Root_M33, &cfg);

  is_applied = true;
#endif

  SystemCoreClock = CLOCK_GetRootClockFreq(kCLOCK_Root_M33);

  return is_applied;
}

bool clockIsApplied(void)
{
  return is_applied;
}

bool clockIsOsc24mOn(void)
{
  return (ANADIG_OSC->OSC_24M_CTRL & ANADIG_OSC_OSC_24M_CTRL_OSC_24M_STABLE_MASK) != 0;
}

uint32_t clockGetRootCount(void)
{
  return sizeof(root_tbl) / sizeof(root_tbl[0]);
}

bool clockGetRootInfo(uint32_t index, clock_info_t *p_info)
{
  if (index >= clockGetRootCount()) return false;

  const clock_root_tbl_t *p = &root_tbl[index];

  p_info->name    = p->name;
  p_info->mux     = CLOCK_GetRootClockMux(p->root);
  p_info->div     = CLOCK_GetRootClockDiv(p->root);
  p_info->calc_hz = CLOCK_GetRootClockFreq(p->root);
  p_info->meas_hz = (p->obs != 0) ? CLOCK_GetFreqFromObs(0, p->obs) : 0;

  return true;
}

uint32_t clockGetPllCount(void)
{
  return sizeof(pll_tbl) / sizeof(pll_tbl[0]);
}

bool clockGetPllInfo(uint32_t index, const char **p_name, bool *p_enable, bool *p_bypass, uint32_t *p_hz)
{
  if (index >= clockGetPllCount()) return false;

  clock_pll_t pll = pll_tbl[index].pll;

  *p_name   = pll_tbl[index].name;
  *p_enable = CLOCK_IsPllEnabled(pll);
  *p_bypass = CLOCK_IsPllBypassed(pll);
  *p_hz     = *p_enable ? CLOCK_GetPllFreq(pll) : 0;

  return true;
}

#if CLOCK_APPLY
static bool pllReady(clock_pll_t pll)
{
  return CLOCK_IsPllEnabled(pll) && !CLOCK_IsPllBypassed(pll);
}
#endif
