#include "cmd_task.h"

#ifdef _USE_HW_CMD
#include "driver/drv_ipc.h"
#include "process/cmd_peer.h"


/* 채널마다 cmd_t 와 cmd_driver_t 를 하나씩 둔다 (N657X0 cmd_task.c 와 같다).
   지금은 CM7 과의 ipc 채널 하나. UART · USB 채널이 생기면 늘린다.
   모듈 등록(MODULE_DEF)은 로드맵 29 에서 들여온다. 그 전까지는 ap.c 가 부른다. */
#define CMD_DRIVER_MAX_CH   1
#define CMD_CH_PEER         0


static cmd_t        cmd[CMD_DRIVER_MAX_CH];
static cmd_driver_t cmd_driver[CMD_DRIVER_MAX_CH];




bool cmdTaskInit(void)
{
  drvIpcInit(&cmd_driver[CMD_CH_PEER]);

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
  //-- CM7 이 먼저 보내는 패킷은 아직 없다. 들어오면 읽어 버린다.
  //
  for (uint32_t i = 0; i < CMD_DRIVER_MAX_CH; i++)
  {
    cmdReceivePacket(&cmd[i]);
  }
  return true;
}

#endif
