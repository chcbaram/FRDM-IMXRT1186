#include "led.h"


#ifdef _USE_HW_LED
#include "fsl_rgpio.h"
#include "fsl_iomuxc.h"
#include "fsl_clock.h"

/*
 * RGB LED D4 — 회로도 4, 7페이지 (docs/03-board-mapping.md 4절)
 *
 *   RED    GPIO_EMC_B1_09  RGPIO2.09  R210 → R197
 *   GREEN  GPIO_EMC_B1_11  RGPIO2.11  R368 → R379
 *   BLUE   GPIO_EMC_B1_39  RGPIO3.07  R84  → R83
 *
 * 전부 active high (SDK board.h LOGIC_LED_ON = 1).
 * 핀마다 3핀 0402 점퍼 두 개를 지나며, 반대쪽은 SRAMC 신호다.
 */

typedef struct
{
  RGPIO_Type *port;
  uint32_t    pin;
  uint32_t    mux[5];        // IOMUXC_GPIO_xxx_GPIOn_IOmm 매크로 그대로
  uint8_t     on_state;
  uint8_t     off_state;
} led_tbl_t;

static const led_tbl_t led_tbl[LED_MAX_CH] =
{
  {RGPIO2, 9,  {IOMUXC_GPIO_EMC_B1_09_GPIO2_IO09}, _DEF_HIGH, _DEF_LOW},   // RED
  {RGPIO2, 11, {IOMUXC_GPIO_EMC_B1_11_GPIO2_IO11}, _DEF_HIGH, _DEF_LOW},   // GREEN
  {RGPIO3, 7,  {IOMUXC_GPIO_EMC_B1_39_GPIO3_IO07}, _DEF_HIGH, _DEF_LOW},   // BLUE
};


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

  RGPIO_PinWrite(led_tbl[ch].port, led_tbl[ch].pin, led_tbl[ch].on_state);
}

void ledOff(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  RGPIO_PinWrite(led_tbl[ch].port, led_tbl[ch].pin, led_tbl[ch].off_state);
}

void ledToggle(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  RGPIO_PortToggle(led_tbl[ch].port, 1U << led_tbl[ch].pin);
}

#endif
