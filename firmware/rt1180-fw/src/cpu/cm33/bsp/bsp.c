#include "bsp.h"
#include "hw_def.h"
#include "fsl_clock.h"
#include "clock.h"

#ifdef _USE_HW_SWTIMER
void swtimerISR(void);
#endif


static volatile uint32_t systick_ms = 0;




bool bspInit(void)
{
  //-- 클럭 루트를 PLL 로 옮긴다 (bsp/clock.c). SystemCoreClock 도 여기서 고쳐진다.
  //   system_MIMXRT1186_cm33.c 의 초기값(240 MHz)은 ROM 상태(200 MHz)와 맞지 않아
  //   CCM 을 읽은 값으로 덮는다.
  //
  clockInit();

  //-- MPU 를 잡고 시스템 버스 캐시(XCACHE_PS)를 켠다 (bsp/mpu.c). CM7 을 깨우기 전에 해야
  //   CM7 TCM alias 와 공유 블록이 non-cacheable 로 잡힌다.
  //
  mpuInit();

  //-- SysTick 은 여기서 켜지 않는다. FreeRTOS 포트(port.c)가 SysTick_Handler 를 가지고
  //   vTaskStartScheduler() 안에서 설정한다. 여기서 미리 인터럽트를 켜면 스케줄러가
  //   없는 상태로 틱 처리가 돌아 죽는다 (titan-mini 와 같다, docs/28).
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

#ifdef _USE_HW_SWTIMER
  swtimerISR();
#endif
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
  //-- configTICK_RATE_HZ 가 1000 이라 틱이 곧 ms 다. 드라이버는 모두 스케줄러 아래(main
  //   스레드)에서 돌므로 millis() 는 항상 유효하다.
  //
  return (uint32_t)xTaskGetTickCount();
#else
  return systick_ms;
#endif
}

uint32_t micros(void)
{
  //-- 틱 사이를 SysTick 카운터로 보간한다. 틱리스 아이들을 켜면 SysTick LOAD 가
  //   바뀌어 이 계산이 깨진다 (FreeRTOSConfig.h configUSE_TICKLESS_IDLE 0).
  //
  uint32_t m0, m1, val;

  do
  {
    m0  = millis();
    val = SysTick->VAL;
    m1  = millis();
  } while (m0 != m1);

  uint32_t load = SysTick->LOAD + 1;

  if (load <= 1) return m0 * 1000;      // SysTick 이 아직 안 돌고 있다

  return m0 * 1000 + ((load - val) * 1000) / load;
}

#ifdef _USE_HW_RTOS
/*
 * FreeRTOS 런타임 통계용 카운터. DWT 사이클(240 MHz)을 8 bit 내려 약 0.94 MHz 로 쓴다.
 * 32 bit 가 한 바퀴 도는 데 약 76 분이다. micros() 는 xTaskGetTickCount() 를 부르므로
 * 태스크 전환 문맥에서 쓰지 않는다 (titan-mini 와 같다).
 */
void bspRunTimeStatsInit(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t bspGetRunTimeCounter(void)
{
  //-- probe-rs 는 연결을 끊을 때 DEMCR 에 0 을 써서 TRCENA 를 끈다. 기록 → 리셋 뒤에
  //   펌웨어가 켜 둔 DWT 가 그때 멈춘다. 꺼져 있으면 다시 켠다 (docs/28).
  //
  if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U)
  {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
  }
  return DWT->CYCCNT >> 8;
}
#endif

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
