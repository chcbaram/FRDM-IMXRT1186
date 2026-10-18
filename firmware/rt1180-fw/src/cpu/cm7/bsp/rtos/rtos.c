#include "hw_def.h"
#include "rtos.h"

#ifdef _USE_HW_RTOS

/*
 * FreeRTOS 훅 — CM7 (docs/28-freertos.md)
 *
 * CM7 에는 UART 도 로그도 없다. 멈춰야 하는 상황에서는 디버거가 붙어 있으면 BKPT 로,
 * 아니면 그 자리에서 멈춘다. CM33 은 alive 가 멈춘 것으로 안다 (ipc info 의 running).
 */

static void rtosHalt(void);




bool rtosInit(void)
{
  return true;
}

bool rtosIsRunning(void)
{
  return (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED);
}

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

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  (void)pcTaskName;
  rtosHalt();
}

void vApplicationMallocFailedHook(void)
{
  rtosHalt();
}

void vAssertCalled(const char *p_file, int line)
{
  (void)p_file;
  (void)line;
  rtosHalt();
}

static void rtosHalt(void)
{
  if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)
  {
    __BKPT(0);
  }
  __disable_irq();
  while (1);
}

#endif
