#include "cmd_task.h"
#include "module.h"


#ifdef _USE_HW_CMD
#include "driver/drv_uart.h"
#include "process/cmd_boot.h"


/* 채널마다 cmd_t 와 cmd_driver_t 를 하나씩 둔다 (N657X0 와 같다).
   지금은 UART 하나. drv_uart.c 는 앱(rt1180-fw)의 것을 같이 쓴다. */
#define CMD_DRIVER_MAX_CH   1


static void cmdTaskThread(void const *arg);

static cmd_t        cmd[CMD_DRIVER_MAX_CH];
static cmd_driver_t cmd_driver[CMD_DRIVER_MAX_CH];


// cli 보다 늦게 연다. drvUartInit() 이 cli 에 RX 필터를 건다
MODULE_DEF(cmd)
{
  .name     = "cmd",
  .priority = MODULE_PRI_LOWEST,
  .init     = cmdTaskInit,
  .update   = cmdTaskThread,
};




bool cmdTaskInit(void)
{
  drvUartInit(&cmd_driver[0], HW_CMD_UART_CH, 115200);

  for (uint32_t i = 0; i < CMD_DRIVER_MAX_CH; i++)
  {
    cmdInit(&cmd[i], &cmd_driver[i]);
    cmdOpen(&cmd[i]);
  }

  return true;
}

bool cmdTaskUpdate(void)
{
  drvUartUpdate(&cmd_driver[0]);

  for (uint32_t i = 0; i < CMD_DRIVER_MAX_CH; i++)
  {
    if (cmdReceivePacket(&cmd[i]) == true)
    {
      cmdBootProcess(&cmd[i]);
    }
  }
  return true;
}

void cmdTaskThread(void const *arg)
{
  (void)arg;
  cmdTaskUpdate();
}

#endif
