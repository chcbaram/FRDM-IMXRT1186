#include "led.h"


#ifdef _USE_HW_LED
#include "pwm.h"
#include "fsl_rgpio.h"
#include "fsl_iomuxc.h"
#include "fsl_clock.h"

/*
 * RGB LED D4 — 회로도 4, 7페이지 (docs/03-board-mapping.md 4절)
 *
 *   RED    GPIO_EMC_B1_09  RGPIO2.09  R210 → R197   (FLEXPWM2_PWMA03 가능)
 *   GREEN  GPIO_EMC_B1_11  RGPIO2.11  R368 → R379   FLEXPWM3_PWMA03 → pwm.c
 *   BLUE   GPIO_EMC_B1_39  RGPIO3.07  R84  → R83    (FLEXPWM1_PWMA03 가능)
 *
 * 전부 active high (SDK board.h LOGIC_LED_ON = 1).
 * 핀마다 3핀 0402 점퍼 두 개를 지나며, 반대쪽은 SRAMC 신호다.
 *
 * 채널마다 GPIO 인지 PWM 인지 테이블에 적는다.
 *   GPIO : 켜고 끄기만 한다. 듀티는 0 이면 끄고 그 외에는 켠다.
 *   PWM  : 켤 때 저장된 듀티(%)를 pwm 채널에 쓴다. 핀 설정은 pwm.c 가 한다.
 *          눈은 밝기를 선형으로 느끼지 않으므로 % 를 제곱해서 듀티로 바꾼다(감마 2).
 */

#define LED_TYPE_GPIO     0
#define LED_TYPE_PWM      1


typedef struct
{
  uint8_t     type;
  RGPIO_Type *port;
  uint32_t    pin;
  uint32_t    mux[5];        // IOMUXC_GPIO_xxx_GPIOn_IOmm 매크로 그대로
  uint8_t     on_state;
  uint8_t     off_state;
  uint8_t     pwm_ch;        // LED_TYPE_PWM 일 때 pwm 채널
} led_tbl_t;


static void ledApply(uint8_t ch);


static const led_tbl_t led_tbl[LED_MAX_CH] =
{
  {LED_TYPE_GPIO, RGPIO2, 9,  {IOMUXC_GPIO_EMC_B1_09_GPIO2_IO09}, _DEF_HIGH, _DEF_LOW, 0},               // RED
  {LED_TYPE_PWM,  RGPIO2, 11, {IOMUXC_GPIO_EMC_B1_11_GPIO2_IO11}, _DEF_HIGH, _DEF_LOW, PWM_LED_GREEN},   // GREEN
  {LED_TYPE_GPIO, RGPIO3, 7,  {IOMUXC_GPIO_EMC_B1_39_GPIO3_IO07}, _DEF_HIGH, _DEF_LOW, 0},               // BLUE
};

static bool    led_on[LED_MAX_CH];
static uint8_t led_duty[LED_MAX_CH];




bool ledInit(void)
{
  rgpio_pin_config_t config =
  {
    .pinDirection = kRGPIO_DigitalOutput,
    .outputLogic  = 0U,
  };

  //-- IOMUXC 레지스터 쓰기에는 LPCG 가 켜져 있어야 한다. (SDK BOARD_InitLEDsPins)
  //
  CLOCK_EnableClock(kCLOCK_Iomuxc1);

  for (int i = 0; i < LED_MAX_CH; i++)
  {
    led_on[i]   = false;
    led_duty[i] = HW_LED_DEF_DUTY;

    if (led_tbl[i].type == LED_TYPE_PWM)
    {
      ledApply(i);
      continue;
    }

    config.outputLogic = led_tbl[i].off_state;
    RGPIO_PinInit(led_tbl[i].port, led_tbl[i].pin, &config);

    IOMUXC_SetPinMux(led_tbl[i].mux[0], led_tbl[i].mux[1], led_tbl[i].mux[2],
                     led_tbl[i].mux[3], led_tbl[i].mux[4], 0U);
  }

  return true;
}

void ledOn(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  led_on[ch] = true;
  ledApply(ch);
}

void ledOff(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  led_on[ch] = false;
  ledApply(ch);
}

void ledToggle(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  led_on[ch] = !led_on[ch];
  ledApply(ch);
}

void ledSetDuty(uint8_t ch, uint8_t duty)
{
  if (ch >= LED_MAX_CH) return;

  led_duty[ch] = constrain(duty, 0, 100);
  ledApply(ch);
}

uint8_t ledGetDuty(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return 0;

  return led_duty[ch];
}

static void ledApply(uint8_t ch)
{
  const led_tbl_t *p_tbl = &led_tbl[ch];

  if (p_tbl->type == LED_TYPE_PWM)
  {
    uint32_t max  = pwmGetMax(p_tbl->pwm_ch);
    uint32_t duty = 0;

    if (led_on[ch] && led_duty[ch] > 0)
    {
      duty = max * led_duty[ch] * led_duty[ch] / (100 * 100);
      if (duty == 0)
        duty = 1;                     // 1% 도 꺼지지 않게
    }
    pwmWrite(p_tbl->pwm_ch, (uint16_t)duty);
  }
  else
  {
    bool on = led_on[ch] && led_duty[ch] > 0;

    RGPIO_PinWrite(p_tbl->port, p_tbl->pin, on ? p_tbl->on_state : p_tbl->off_state);
  }
}

#endif
