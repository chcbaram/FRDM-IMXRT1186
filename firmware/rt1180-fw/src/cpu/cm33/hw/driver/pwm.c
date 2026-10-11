#include "pwm.h"


#ifdef _USE_HW_PWM
#include "cli.h"
#include "log.h"
#include "fsl_pwm.h"
#include "fsl_iomuxc.h"
#include "fsl_clock.h"

/*
 * PWM — FlexPWM (RM 70장), stm32h5-bd 의 pwm.c 와 같은 API
 *
 *   클럭 : FlexPWM1~4 는 bus_wakeup_clk_root (RM CCM 표, LPCG123~126).
 *          clock.c 가 132 MHz 로 둔다. 프리스케일 1 → 20 kHz 면 한 주기 6600 틱.
 *   fault: SMxDISMAP0 의 리셋값이 FFFFh 라 fault 입력이 출력을 끈다. SDK 예제는
 *          XBAR 로 fault 입력을 High 에 묶지만 여기서는 fault 를 쓰지 않으므로
 *          DISMAP 을 0 으로 둔다. XBAR 드라이버가 필요 없다.
 *   듀티 : 0 ~ PWM_MAX_DUTY. PWM_UpdatePwmDutycycleHighAccuracy (0~65535) 로 바꿔 쓴다.
 */

#define NAME_DEF(x)  x, #x

#define PWM_FREQ_HZ       20000
#define PWM_MAX_DUTY      1000


typedef struct
{
  PWM_Type        *base;
  pwm_submodule_t  sm;
  pwm_channels_t   out;           // PWM A / B
  uint32_t         mux[5];        // IOMUXC_GPIO_xxx_FLEXPWMn_PWMxnn 매크로 그대로
  uint16_t         max_value;
  uint32_t         pwm_freq_hz;
  PwmPinName_t     pin_name;
  const char      *p_name;
} pwm_tbl_t;


#if CLI_USE(HW_PWM)
static void cliPwm(cli_args_t *args);
#endif
static bool pwmInitCh(uint8_t ch);


static bool     is_init = false;
static uint16_t pwm_duty[PWM_MAX_CH] = {0, };

// LED_GREEN : GPIO_EMC_B1_11 → FLEXPWM3 서브모듈 3 A (docs/03-board-mapping.md 4절)
//
static const pwm_tbl_t pwm_tbl[PWM_MAX_CH] =
{
  {PWM3, kPWM_Module_3, kPWM_PwmA, {IOMUXC_GPIO_EMC_B1_11_FLEXPWM3_PWMA03}, PWM_MAX_DUTY, PWM_FREQ_HZ, NAME_DEF(PWM_LED_GREEN)},
};




bool pwmInit(void)
{
  bool ret = true;

  for (int i = 0; i < PWM_MAX_CH; i++)
  {
    assert(i == (int)pwm_tbl[i].pin_name);
  }

  //-- IOMUXC 레지스터 쓰기에는 LPCG 가 켜져 있어야 한다.
  //
  CLOCK_EnableClock(kCLOCK_Iomuxc1);

  for (int i = 0; i < PWM_MAX_CH; i++)
  {
    if (pwmInitCh(i) != true)
    {
      ret = false;
    }
  }

  is_init = ret;

  logPrintf("[%s] pwmInit()\n", ret ? "OK":"E_");

#if CLI_USE(HW_PWM)
  cliAdd("pwm", cliPwm);
#endif
  return ret;
}

bool pwmIsInit(void)
{
  return is_init;
}

void pwmWrite(uint8_t ch, uint16_t pwm_data)
{
  if (ch >= PWM_MAX_CH) return;

  const pwm_tbl_t *p_tbl = &pwm_tbl[ch];

  pwm_duty[ch] = constrain(pwm_data, 0, p_tbl->max_value);

  //-- VAL 레지스터는 버퍼다. LDOK 를 세우면 다음 주기 시작에 반영된다.
  //
  PWM_UpdatePwmDutycycleHighAccuracy(p_tbl->base, p_tbl->sm, p_tbl->out, kPWM_EdgeAligned,
                                     (uint16_t)((uint32_t)pwm_duty[ch] * 0xFFFFU / p_tbl->max_value));
  PWM_SetPwmLdok(p_tbl->base, 1U << p_tbl->sm, true);
}

uint16_t pwmRead(uint8_t ch)
{
  if (ch >= PWM_MAX_CH) return 0;

  return pwm_duty[ch];
}

uint16_t pwmGetMax(uint8_t ch)
{
  if (ch >= PWM_MAX_CH) return PWM_MAX_DUTY;

  return pwm_tbl[ch].max_value;
}

static bool pwmInitCh(uint8_t ch)
{
  const pwm_tbl_t   *p_tbl = &pwm_tbl[ch];
  pwm_config_t       config;
  pwm_signal_param_t signal =
  {
    .pwmChannel       = p_tbl->out,
    .dutyCyclePercent = 0,
    .level            = kPWM_HighTrue,
    .deadtimeValue    = 0,
    .faultState       = kPWM_PwmFaultState0,
    .pwmchannelenable = true,
  };

  //-- 기본값 : IP 버스 클럭, 프리스케일 1, 독립 출력, 한 주기 끝에서 reload
  //
  PWM_GetDefaultConfig(&config);
  config.reloadLogic = kPWM_ReloadPwmFullCycle;
  config.pairOperation = kPWM_Independent;

  if (PWM_Init(p_tbl->base, p_tbl->sm, &config) != kStatus_Success)
    return false;

  p_tbl->base->SM[p_tbl->sm].DISMAP[0] = 0;

  if (PWM_SetupPwm(p_tbl->base, p_tbl->sm, &signal, 1, kPWM_EdgeAligned,
                   p_tbl->pwm_freq_hz, CLOCK_GetRootClockFreq(kCLOCK_Root_Bus_Wakeup)) != kStatus_Success)
    return false;

  PWM_SetPwmLdok(p_tbl->base, 1U << p_tbl->sm, true);
  PWM_StartTimer(p_tbl->base, 1U << p_tbl->sm);

  //-- 듀티 0 으로 타이머가 돈 뒤에 핀을 넘긴다. 켜지는 순간 번쩍이지 않는다.
  //
  IOMUXC_SetPinMux(p_tbl->mux[0], p_tbl->mux[1], p_tbl->mux[2], p_tbl->mux[3], p_tbl->mux[4], 0U);

  return true;
}


#if CLI_USE(HW_PWM)
void cliPwm(cli_args_t *args)
{
  bool     ret = false;
  uint8_t  ch;
  uint16_t pwm;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    for (int i = 0; i < PWM_MAX_CH; i++)
    {
      cliPrintf("%02d. %-16s %3dKhz : %04d/%04d %3d%%\n",
                i,
                pwm_tbl[i].p_name,
                (int)(pwm_tbl[i].pwm_freq_hz / 1000),
                pwm_duty[i],
                pwm_tbl[i].max_value,
                pwm_duty[i] * 100 / pwm_tbl[i].max_value);
    }
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "set"))
  {
    ch  = (uint8_t)args->getData(1);
    pwm = (uint16_t)args->getData(2);

    if (ch < PWM_MAX_CH)
    {
      pwmWrite(ch, pwm);
      cliPrintf("pwm ch%d %d\n", ch, pwmRead(ch));
      ret = true;
    }
  }

  if (args->argc == 2 && args->isStr(0, "get"))
  {
    ch = (uint8_t)args->getData(1);

    if (ch < PWM_MAX_CH)
    {
      cliPrintf("pwm ch%d %d\n", ch, pwmRead(ch));
      ret = true;
    }
  }

  if (ret == false)
  {
    cliPrintf("pwm info\n");
    cliPrintf("pwm set 0~%d 0~%d\n", PWM_MAX_CH - 1, PWM_MAX_DUTY);
    cliPrintf("pwm get 0~%d\n", PWM_MAX_CH - 1);
  }
}
#endif

#endif
