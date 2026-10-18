#include "hw_def.h"
#include "rtos.h"

#ifdef _USE_HW_RTOS
#include "log.h"

/*
 * FreeRTOS 훅 — titan-mini 의 rtos.c 를 따른다 (docs/28-freertos.md)
 */

#ifdef _USE_HW_SWTIMER
void swtimerISR(void);
#endif




bool rtosInit(void)
{
  return true;
}

bool rtosIsRunning(void)
{
  return (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED);
}

/*
 * SysTick_Handler 는 FreeRTOS 포트(port.c)가 가진다. 그 전에는 bsp.c 가 1 ms 마다
 * swtimerISR() 를 불렀다. 같은 일을 틱 훅에서 한다 (틱 = 1 ms, ISR 문맥).
 */
void vApplicationTickHook(void)
{
#ifdef _USE_HW_SWTIMER
  swtimerISR();
#endif
}

/*
 * configSUPPORT_STATIC_ALLOCATION 을 켜면 커널이 idle / timer 태스크의 메모리를
 * 애플리케이션에서 받아 간다. 두 콜백을 주지 않으면 링크가 깨진다.
 */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t  **ppxIdleTaskStackBuffer,
                                   uint32_t      *pulIdleTaskStackSize)
{
  static StaticTask_t idle_tcb;
  static StackType_t  idle_stack[configMINIMAL_STACK_SIZE];

  *ppxIdleTaskTCBBuffer   = &idle_tcb;
  *ppxIdleTaskStackBuffer = idle_stack;
  *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t  **ppxTimerTaskStackBuffer,
                                    uint32_t      *pulTimerTaskStackSize)
{
  static StaticTask_t timer_tcb;
  static StackType_t  timer_stack[configTIMER_TASK_STACK_DEPTH];

  *ppxTimerTaskTCBBuffer   = &timer_tcb;
  *ppxTimerTaskStackBuffer = timer_stack;
  *pulTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
}


//-- 아래 훅들은 조용히 넘어가면 안 되는 상황이다. 로그를 남기고 멈춘다.
//   디버거가 붙어 있으면 여기서 잡힌다.
//
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;

  logPrintf("\r\nStackOverflow : %s\r\n", pcTaskName);

  if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)
  {
    __BKPT(0);
  }
  while (1);
}

void vApplicationMallocFailedHook(void)
{
  logPrintf("\r\nMallocFailed\r\n");

  if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)
  {
    __BKPT(0);
  }
  while (1);
}

void vAssertCalled(const char *p_file, int line)
{
  logPrintf("\r\nassert : %s:%d\r\n", p_file, line);

  if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)
  {
    __BKPT(0);
  }
  __disable_irq();
  while (1);
}

#endif
