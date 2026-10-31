#include "cmd_task.h"
#include "module.h"

#ifdef _USE_HW_CMD
#include "driver/drv_ipc.h"
#include "driver/drv_uart.h"
#include "process/cmd_peer.h"
#include "process/cmd_boot.h"


/* 채널마다 cmd_t 와 cmd_driver_t 를 하나씩 둔다 (N657X0 cmd_task.c 와 같다).
   PEER : CM7 과의 ipc 채널. CM7 을 함께 빌드하지 않아도 연다. 스트림이 열리지 않았으면 아무것도 오가지 않는다.
   UART : CLI 와 같은 UART. download.py 가 붙으면 부트로더로 넘긴다 (process/cmd_boot.c, docs/43). */
#define CMD_DRIVER_MAX_CH   2
#define CMD_CH_PEER         0
#define CMD_CH_UART         1


static void cmdTaskThread(void const *arg);

static cmd_t        cmd[CMD_DRIVER_MAX_CH];
static cmd_driver_t cmd_driver[CMD_DRIVER_MAX_CH];
#ifdef _USE_HW_RTOS
static SemaphoreHandle_t cmd_lock = NULL;
#endif


MODULE_DEF(cmd)
{
  .name     = "cmd",
  .priority = MODULE_PRI_LOWEST,
  .init     = cmdTaskInit,
  .update   = cmdTaskThread,
};




bool cmdTaskInit(void)
{
#ifdef _USE_HW_RTOS
  cmd_lock = xSemaphoreCreateMutex();
#endif
  drvIpcInit(&cmd_driver[CMD_CH_PEER]);
  drvUartInit(&cmd_driver[CMD_CH_UART], HW_CMD_UART_CH, 115200);

  for (uint32_t i = 0; i < CMD_DRIVER_MAX_CH; i++)
  {
    cmdInit(&cmd[i], &cmd_driver[i]);
    cmdOpen(&cmd[i]);
  }

  cmdPeerInit(&cmd[CMD_CH_PEER]);
  return true;
}

bool cmdTaskUpdate(void)
{
  //-- UART 는 download.py 의 요청을 처리한다.
  //
  if (cmdReceivePacket(&cmd[CMD_CH_UART]) == true)
  {
    cmdBootProcess(&cmd[CMD_CH_UART]);
  }

  //-- CM7 이 먼저 보내는 패킷은 아직 없다. 들어오면 읽어 버린다.
  //   다른 스레드가 명령을 주고받는 중이면(잠겨 있으면) 이번 차례는 건너뛴다.
  //
  if (cmdTaskLock(0) != true)
    return false;

  cmdReceivePacket(&cmd[CMD_CH_PEER]);

  cmdTaskUnlock();
  return true;
}

bool cmdTaskLock(uint32_t timeout_ms)
{
#ifdef _USE_HW_RTOS
  if (cmd_lock == NULL)
    return false;
  return (xSemaphoreTake(cmd_lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
#else
  (void)timeout_ms;
  return true;
#endif
}

void cmdTaskUnlock(void)
{
#ifdef _USE_HW_RTOS
  if (cmd_lock != NULL)
    xSemaphoreGive(cmd_lock);
#endif
}

void cmdTaskThread(void const *arg)
{
  (void)arg;
  cmdTaskUpdate();
}

#endif
