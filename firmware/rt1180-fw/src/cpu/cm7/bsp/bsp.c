#include "bsp.h"
#include "hw_def.h"
#include "fsl_clock.h"


#ifndef _USE_HW_RTOS
static volatile uint32_t systick_ms = 0;
#endif




bool bspInit(void)
{
  //-- CM7 클럭은 CM33 이 깨우기 전에 잡아 둔다 (ARM PLL 792 MHz, cm33/bsp/cm7.c).
  //   system_MIMXRT1186_cm7.c 의 초기값도 792 MHz 지만 CCM 에서 읽어 확인한다.
  //
  SystemCoreClock = CLOCK_GetRootClockFreq(kCLOCK_Root_M7);

  //-- SysTick 은 FreeRTOS 포트가 vTaskStartScheduler() 안에서 켠다 (docs/28).
  //
#ifndef _USE_HW_RTOS
  SysTick_Config(SystemCoreClock / 1000);   // 1ms
#endif

  return true;
}

#ifndef _USE_HW_RTOS
void SysTick_Handler(void)
{
  systick_ms++;
}
#endif

void delay(uint32_t ms)
{
#ifdef _USE_HW_RTOS
  if (rtosIsRunning())
  {
    vTaskDelay(pdMS_TO_TICKS(ms));
    return;
  }

  //-- 스케줄러 전에는 틱이 없다. DWT 사이클로 기다린다.
  //
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
  uint32_t start = DWT->CYCCNT;
  uint32_t cyc   = ms * (SystemCoreClock / 1000U);
  while ((DWT->CYCCNT - start) < cyc);
#else
  uint32_t pre_time = systick_ms;

  while (systick_ms - pre_time < ms);
#endif
}

uint32_t millis(void)
{
#ifdef _USE_HW_RTOS
  return (uint32_t)xTaskGetTickCount();
#else
  return systick_ms;
#endif
}

#ifdef _USE_HW_RTOS
/*
 * FreeRTOS 런타임 통계용 카운터. DWT 사이클(792 MHz) >> 8 = 약 3.1 MHz, 약 23 분에 한 바퀴.
 * probe-rs 가 연결을 끊으며 DEMCR 을 지우면 다시 켠다 (CM33 bsp.c 와 같다).
 */
void bspRunTimeStatsInit(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t bspGetRunTimeCounter(void)
{
  if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U)
  {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
  }
  return DWT->CYCCNT >> 8;
}
#endif
