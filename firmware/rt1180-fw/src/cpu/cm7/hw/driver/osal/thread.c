#include "osal/thread.h"

#ifdef _USE_HW_THREAD

#include "rtos.h"
#include "log.h"
#include "cli.h"


typedef struct
{
  const char   *name;
  thread_func_t func;
  void         *arg;
  uint32_t      priority;
  uint32_t      stack_bytes;
  TaskHandle_t  handle;
} thread_tbl_t;


#define CPU_USAGE_WINDOW_MS   1000


#if CLI_USE(HW_THREAD)
static void cliThread(cli_args_t *args);
static void cliShowCpuUsage(void);
#endif

static thread_tbl_t thread_tbl[THREAD_MAX_CNT];
static uint32_t     thread_cnt = 0;
static bool         is_init    = false;


bool threadInit(void)
{
  thread_cnt = 0;
  is_init    = true;

#if CLI_USE(HW_THREAD)
  cliAdd("thread", cliThread);
#endif

  return true;
}

thread_id_t threadCreate(const char   *name,
                         thread_func_t func,
                         void         *arg,
                         uint32_t      priority,
                         uint32_t      stack_bytes)
{
  if (is_init != true) return -1;
  if (thread_cnt >= THREAD_MAX_CNT) return -1;
  if (func == NULL) return -1;

  thread_id_t id = (thread_id_t)thread_cnt;

  thread_tbl[id].name        = name;
  thread_tbl[id].func        = func;
  thread_tbl[id].arg         = arg;
  thread_tbl[id].priority    = priority;
  thread_tbl[id].stack_bytes = stack_bytes;
  thread_tbl[id].handle      = NULL;

  thread_cnt++;

  return id;
}

bool threadBegin(void)
{
  for (uint32_t i = 0; i < thread_cnt; i++)
  {
    BaseType_t ret;

    //-- FreeRTOS 의 스택 인자는 바이트가 아니라 워드 단위다.
    //
    ret = xTaskCreate((TaskFunction_t)thread_tbl[i].func,
                      thread_tbl[i].name,
                      thread_tbl[i].stack_bytes / sizeof(StackType_t),
                      thread_tbl[i].arg,
                      thread_tbl[i].priority,
                      &thread_tbl[i].handle);

    if (ret != pdPASS)
    {
      logPrintf("threadBegin : %s 생성 실패\r\n", thread_tbl[i].name);
      return false;
    }
  }

  //-- 스케줄러는 main.c 가 이미 시작했다. 여기서는 생성만 한다.
  //
  return true;
}

uint32_t threadGetCount(void)
{
  return thread_cnt;
}


#if CLI_USE(HW_THREAD)
void cliThread(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("thread cnt : %d / %d\n", thread_cnt, THREAD_MAX_CNT);
    cliPrintf("heap free  : %d / %d bytes\n",
              (int)xPortGetFreeHeapSize(), configTOTAL_HEAP_SIZE);
    cliPrintf("heap min   : %d bytes\n", (int)xPortGetMinimumEverFreeHeapSize());
    cliPrintf("\n");
    cliPrintf("%-16s %-4s %-6s %s\n", "name", "pri", "stack", "free(word)");

    for (uint32_t i = 0; i < thread_cnt; i++)
    {
      cliPrintf("%-16s %-4d %-6d %d\n",
                thread_tbl[i].name,
                (int)thread_tbl[i].priority,
                (int)thread_tbl[i].stack_bytes,
                thread_tbl[i].handle != NULL
                  ? (int)uxTaskGetStackHighWaterMark(thread_tbl[i].handle) : -1);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "cpu"))
  {
    cliShowCpuUsage();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("thread info\n");
    cliPrintf("thread cpu\n");
  }
}

/*
 * 두 스냅샷의 런타임 카운터 차이로 구간 점유율을 낸다.
 *
 * vTaskGetRunTimeStats() 는 부팅 이후 누적이라 "방금 무엇이 CPU 를 먹는가" 를
 * 알 수 없다. 1 초 간격으로 두 번 떠서 차이를 본다.
 *
 * 카운터 원천은 DWT 사이클 카운터다. (bsp.c 의 bspGetRunTimeCounter)
 */
void cliShowCpuUsage(void)
{
  TaskStatus_t *p_pre;
  TaskStatus_t *p_cur;
  uint32_t      pre_total;
  uint32_t      cur_total;
  uint32_t      task_cnt;
  uint32_t      total;

  task_cnt = uxTaskGetNumberOfTasks();

  p_pre = (TaskStatus_t *)pvPortMalloc(task_cnt * sizeof(TaskStatus_t));
  p_cur = (TaskStatus_t *)pvPortMalloc(task_cnt * sizeof(TaskStatus_t));

  if (p_pre == NULL || p_cur == NULL)
  {
    cliPrintf("pvPortMalloc fail\n");
    vPortFree(p_pre);
    vPortFree(p_cur);
    return;
  }

  task_cnt = uxTaskGetSystemState(p_pre, task_cnt, &pre_total);
  delay(CPU_USAGE_WINDOW_MS);
  uxTaskGetSystemState(p_cur, task_cnt, &cur_total);

  total = cur_total - pre_total;

  if (total == 0)
  {
    cliPrintf("run time counter 가 돌지 않는다\n");
  }
  else
  {
    cliPrintf("%-16s %4s %7s\n", "name", "pri", "cpu");

    for (uint32_t i = 0; i < task_cnt; i++)
    {
      uint32_t used = 0;

      //-- 두 스냅샷 사이에 태스크 순서가 바뀔 수 있어 번호로 짝을 찾는다.
      //
      for (uint32_t j = 0; j < task_cnt; j++)
      {
        if (p_cur[j].xTaskNumber == p_pre[i].xTaskNumber)
        {
          used = p_cur[j].ulRunTimeCounter - p_pre[i].ulRunTimeCounter;
          break;
        }
      }

      cliPrintf("%-16s %4d %4d.%d %%\n",
                p_pre[i].pcTaskName,
                (int)p_pre[i].uxCurrentPriority,
                (int)(used * 100 / total),
                (int)(used * 1000 / total % 10));
    }
  }

  vPortFree(p_pre);
  vPortFree(p_cur);
}
#endif

#endif
