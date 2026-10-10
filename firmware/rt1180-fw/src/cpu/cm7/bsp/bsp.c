#include "bsp.h"
#include "hw_def.h"
#include "fsl_clock.h"


static volatile uint32_t systick_ms = 0;




bool bspInit(void)
{
  //-- CM7 클럭은 CM33 이 깨우기 전에 잡아 둔다 (ARM PLL 792 MHz, cm33/bsp/cm7.c).
  //   system_MIMXRT1186_cm7.c 의 초기값도 792 MHz 지만 CCM 에서 읽어 확인한다.
  //
  SystemCoreClock = CLOCK_GetRootClockFreq(kCLOCK_Root_M7);

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
