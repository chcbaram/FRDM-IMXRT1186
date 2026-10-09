#include "bsp.h"
#include "hw_def.h"
#include "fsl_clock.h"
#include "clock.h"


static volatile uint32_t systick_ms = 0;




bool bspInit(void)
{
  //-- 클럭 루트를 PLL 로 옮긴다 (bsp/clock.c). SystemCoreClock 도 여기서 고쳐진다.
  //   system_MIMXRT1186_cm33.c 의 초기값(240 MHz)은 ROM 상태(200 MHz)와 맞지 않아
  //   CCM 을 읽은 값으로 덮는다.
  //
  clockInit();

  SysTick_Config(SystemCoreClock / 1000);   // 1ms

  return true;
}

void SysTick_Handler(void)
{
  systick_ms++;
}

void delay(uint32_t ms)
{
  uint32_t pre_time = systick_ms;

  while (systick_ms - pre_time < ms);
}

uint32_t millis(void)
{
  return systick_ms;
}

uint32_t micros(void)
{
  //-- 틱 사이를 SysTick 카운터로 보간한다.
  //
  uint32_t m0, m1, val;

  do
  {
    m0  = systick_ms;
    val = SysTick->VAL;
    m1  = systick_ms;
  } while (m0 != m1);

  uint32_t load = SysTick->LOAD + 1;

  return m0 * 1000 + ((load - val) * 1000) / load;
}

void Error_Handler(void)
{
  if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)
  {
    __BKPT(0);
  }

  __disable_irq();
  while (1)
  {
  }
}
