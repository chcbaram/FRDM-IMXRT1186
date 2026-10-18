#include "cmd_task.h"

#ifdef _USE_HW_CMD
#include "driver/drv_ipc.h"
#include "process/cmd_peer.h"


/* 채널마다 cmd_t 와 cmd_driver_t 를 하나씩 둔다 (N657X0 cmd_task.c 와 같다).
   지금은 CM33 과의 ipc 채널 하나. CM7 은 아직 모듈 구조가 없어 ap.c 가 부른다.
   RTOS 에서는 cmd 스레드가 doorbell 에 깨어 처리한다 (ipcStreamWait, docs/28). */
#define CMD_DRIVER_MAX_CH   1


static cmd_t        cmd[CMD_DRIVER_MAX_CH];
static cmd_driver_t cmd_driver[CMD_DRIVER_MAX_CH];




bool cmdTaskInit(void)
{
  drvIpcInit(&cmd_driver[0]);

  for (uint32_t i = 0; i < CMD_DRIVER_MAX_CH; i++)
  {
    cmdInit(&cmd[i], &cmd_driver[i]);
    cmdOpen(&cmd[i]);
  }

  return true;
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

#endif
