#include "bsp.h"
#include "hw_def.h"
#include "fsl_clock.h"


static volatile uint32_t systick_ms = 0;




bool bspInit(void)
{
  //-- 클럭은 아직 건드리지 않는다. BootROM 이 설정해 둔 그대로 쓴다.
  //   BOOT_FREQ 퓨즈가 0 이라 CM33 은 RCOSC200M 에서 200 MHz 로 넘어온다.
  //   (docs/01-boot-sequence.md 3절) PLL 설정은 로드맵 22 에서 한다.
  //
  //   system_MIMXRT1186_cm33.c 의 SystemCoreClock 초기값(240 MHz)은 BOOT_FREQ=1
  //   기준이라 맞지 않는다. CCM 의 클럭 루트를 실제로 읽어 고친다.
  //
  SystemCoreClock = CLOCK_GetRootClockFreq(kCLOCK_Root_M33);

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
