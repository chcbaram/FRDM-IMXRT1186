#include "cmd_task.h"
#include "module.h"

#ifdef _USE_HW_CMD
#include "driver/drv_ipc.h"
#include "process/cmd_peer.h"


/* 채널마다 cmd_t 와 cmd_driver_t 를 하나씩 둔다 (N657X0 cmd_task.c 와 같다).
   지금은 CM33 과의 ipc 채널 하나. 모듈 init 이 cmd 스레드를 등록하고, 스레드는
   doorbell 에 깨어 처리한다 (ipcStreamWait, docs/28). */
#define CMD_DRIVER_MAX_CH   1


static void cmdThread(void *arg);

static cmd_t        cmd[CMD_DRIVER_MAX_CH];
static cmd_driver_t cmd_driver[CMD_DRIVER_MAX_CH];


MODULE_DEF(cmd)
{
  .name     = "cmd",
  .priority = MODULE_PRI_LOWEST,
  .init     = cmdTaskInit,
};




bool cmdTaskInit(void)
{
  drvIpcInit(&cmd_driver[0]);

  for (uint32_t i = 0; i < CMD_DRIVER_MAX_CH; i++)
  {
    cmdInit(&cmd[i], &cmd_driver[i]);
    cmdOpen(&cmd[i]);
  }

  return (threadCreate("cmd", cmdThread, NULL,
                       _HW_DEF_THREAD_CMD_PRI, _HW_DEF_THREAD_CMD_STACK) >= 0);
}

bool cmdTaskUpdate(void)
{
  bool ret = false;

  for (uint32_t i = 0; i < CMD_DRIVER_MAX_CH; i++)
  {
    if (cmdReceivePacket(&cmd[i]) == true)
    {
      cmdPeerProcess(&cmd[i]);
      ret = true;
    }
  }
  return ret;
}

/*
 * CM33 에서 오는 cmd 패킷. 링에 쓰고 doorbell 을 울리면 깨어서(ipcStreamWait) 다 읽는다.
 * 기다리는 동안은 CPU 를 쓰지 않는다. 100 ms 마다는 doorbell 이 없어도 한 번 본다.
 */
void cmdThread(void *arg)
{
  (void)arg;

  logPrintf("[  ] Thread Started : cmd\r\n");

  while (1)
  {
    if (ipcStreamWait(100))
    {
      while (cmdTaskUpdate());
    }
  }
}

#endif
