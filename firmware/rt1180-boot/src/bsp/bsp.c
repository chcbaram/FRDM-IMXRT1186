#include "bsp.h"
#include "clock.h"


static volatile uint32_t systick_ms = 0;




bool bspInit(void)
{
  //-- 클럭 루트를 PLL 로 옮긴다 (앱의 bsp/clock.c 를 같이 쓴다). 앱은 점프 뒤에
  //   같은 clockInit() 을 한 번 더 부른다 (docs/40 8절).
  //
  clockInit();

  //-- RTOS 가 없으므로 SysTick 이 1 ms 틱이다. MPU · XCACHE_PS 는 켜지 않는다.
  //   부트로더는 Code TCM · System TCM 만 쓰고, 점프할 때 되돌릴 것도 줄어든다 (docs/40 5절).
  //
  SysTick_Config(SystemCoreClock / 1000);

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
  uint32_t m0, m1, val;

  do
  {
    m0  = millis();
    val = SysTick->VAL;
    m1  = millis();
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
