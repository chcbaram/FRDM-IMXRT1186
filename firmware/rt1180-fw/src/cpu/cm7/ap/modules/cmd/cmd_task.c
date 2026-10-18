#include "cmd_task.h"

#ifdef _USE_HW_CMD
#include "driver/drv_ipc.h"
#include "process/cmd_peer.h"


/* 채널마다 cmd_t 와 cmd_driver_t 를 하나씩 둔다 (N657X0 cmd_task.c 와 같다).
   지금은 CM33 과의 ipc 채널 하나. 모듈 등록(MODULE_DEF)은 로드맵 29 에서 들여온다.
   그 전까지는 ap.c 가 cmdTaskInit / cmdTaskUpdate 를 부른다. */
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
  for (uint32_t i = 0; i < CMD_DRIVER_MAX_CH; i++)
  {
    if (cmdReceivePacket(&cmd[i]) == true)
    {
      cmdPeerProcess(&cmd[i]);
    }
  }
  return true;
}

#endif
