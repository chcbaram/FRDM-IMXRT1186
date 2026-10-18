#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*
 * FreeRTOS 설정 — CM33 (docs/28-freertos.md). titan-mini 의 설정을 바탕으로 했다.
 *
 * 포트는 ARM_CM33_NTZ (TrustZone 없음). 이 칩은 Secure 상태로만 돈다(SAU 를 켜지 않는다).
 * 그래서 configRUN_FREERTOS_SECURE_ONLY = 1, configENABLE_TRUSTZONE = 0 이다
 * (port.c 머리말의 세 조합 중 첫째). 이 값이 틀리면 EXC_RETURN 이 Non-secure 로 잡혀
 * 첫 태스크로 넘어가는 순간 fault 가 난다.
 */
#ifndef __ASSEMBLER__
 #include <stdint.h>
extern uint32_t SystemCoreClock;
#endif

//-- ARMv8-M 포트
#define configENABLE_TRUSTZONE                    0
#define configRUN_FREERTOS_SECURE_ONLY            1
#define configENABLE_MPU                          0   // MPU 는 bsp/mpu.c 가 쓴다
#define configENABLE_FPU                          1
#define configENABLE_MVE                          0

#define configUSE_PREEMPTION                      1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION   0
#define configUSE_TIME_SLICING                    1
#define configIDLE_SHOULD_YIELD                   1
#define configCPU_CLOCK_HZ                        (SystemCoreClock)
#define configTICK_RATE_HZ                        (1000)          // 틱 = 1 ms (millis)
#define configMAX_PRIORITIES                      (8)
#define configMINIMAL_STACK_SIZE                  (256)
#define configMAX_TASK_NAME_LEN                   (16)
#define configTICK_TYPE_WIDTH_IN_BITS             TICK_TYPE_WIDTH_32_BITS
#define configUSE_TICKLESS_IDLE                   0               // micros() 보간이 SysTick LOAD 를 믿는다

#define configSUPPORT_STATIC_ALLOCATION           1
#define configSUPPORT_DYNAMIC_ALLOCATION          1
#define configTOTAL_HEAP_SIZE                     (32 * 1024)     // System TCM (heap_4)
#define configAPPLICATION_ALLOCATED_HEAP          0

#define configUSE_IDLE_HOOK                       0
#define configUSE_TICK_HOOK                       1               // swtimer 1 ms (rtos.c)
#define configUSE_MALLOC_FAILED_HOOK              1
#define configCHECK_FOR_STACK_OVERFLOW            2
#define configUSE_DAEMON_TASK_STARTUP_HOOK        0

#define configUSE_MUTEXES                         1
#define configUSE_RECURSIVE_MUTEXES               1
#define configUSE_COUNTING_SEMAPHORES             1
#define configUSE_TASK_NOTIFICATIONS              1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES     1
#define configQUEUE_REGISTRY_SIZE                 8
#define configUSE_QUEUE_SETS                      0
#define configUSE_APPLICATION_TASK_TAG            0
#define configENABLE_BACKWARD_COMPATIBILITY       1
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS   0
#define configSTACK_DEPTH_TYPE                    uint32_t
#define configMESSAGE_BUFFER_LENGTH_TYPE          size_t

#define configUSE_TIMERS                          1
#define configTIMER_TASK_PRIORITY                 (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                  16
#define configTIMER_TASK_STACK_DEPTH              (512)

#define configUSE_TRACE_FACILITY                  1
#define configUSE_STATS_FORMATTING_FUNCTIONS      0
#define configGENERATE_RUN_TIME_STATS             1
#ifndef __ASSEMBLER__
void     bspRunTimeStatsInit(void);
uint32_t bspGetRunTimeCounter(void);
 #define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()  bspRunTimeStatsInit()
 #define portGET_RUN_TIME_COUNTER_VALUE()          bspGetRunTimeCounter()

void vAssertCalled(const char *p_file, int line);
 #define configASSERT(x)    if ((x) == 0) { vAssertCalled(__FILE__, __LINE__); }
#endif

//-- 인터럽트 우선순위. 이 칩의 NVIC 는 3 bit (0~7, __NVIC_PRIO_BITS).
//   우선순위 값이 2 이상인 ISR 만 FromISR API 를 부를 수 있다. UART · MU 는 5 다.
//
#define configPRIO_BITS                              3
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      7
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 2
#define configKERNEL_INTERRUPT_PRIORITY      (configLIBRARY_LOWEST_INTERRUPT_PRIORITY      << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define INCLUDE_vTaskPrioritySet                  1
#define INCLUDE_uxTaskPriorityGet                 1
#define INCLUDE_vTaskDelete                       1
#define INCLUDE_vTaskSuspend                      1
#define INCLUDE_vTaskDelayUntil                   1
#define INCLUDE_vTaskDelay                        1
#define INCLUDE_xTaskGetSchedulerState            1
#define INCLUDE_xTaskGetCurrentTaskHandle         1
#define INCLUDE_uxTaskGetStackHighWaterMark       1
#define INCLUDE_xTaskGetIdleTaskHandle            1
#define INCLUDE_eTaskGetState                     1
#define INCLUDE_xTimerPendFunctionCall            1
#define INCLUDE_xTaskAbortDelay                   1
#define INCLUDE_xQueueGetMutexHolder              1
#define INCLUDE_xSemaphoreGetMutexHolder          1

#endif /* FREERTOS_CONFIG_H */
