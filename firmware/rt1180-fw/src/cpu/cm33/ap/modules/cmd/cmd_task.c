#include "cmd_task.h"
#include "module.h"

#ifdef _USE_HW_CMD
#include "driver/drv_ipc.h"
#include "process/cmd_peer.h"


/* 채널마다 cmd_t 와 cmd_driver_t 를 하나씩 둔다 (N657X0 cmd_task.c 와 같다).
   지금은 CM7 과의 ipc 채널 하나. UART · USB 채널이 생기면 늘린다.
   CM7 을 함께 빌드하지 않아도 채널은 연다. ipc 스트림이 열리지 않았으면 아무것도 오가지 않는다. */
#define CMD_DRIVER_MAX_CH   1
#define CMD_CH_PEER         0


static void cmdTaskThread(void const *arg);

static cmd_t        cmd[CMD_DRIVER_MAX_CH];
static cmd_driver_t cmd_driver[CMD_DRIVER_MAX_CH];


MODULE_DEF(cmd)
{
  .name     = "cmd",
  .priority = MODULE_PRI_LOWEST,
  .init     = cmdTaskInit,
  .update   = cmdTaskThread,
};




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

void cmdTaskThread(void const *arg)
{
  (void)arg;
  cmdTaskUpdate();
}

#endif
