#include "process/cmd_peer.h"

#ifdef _USE_HW_CMD
#include "shared.h"
#include <string.h>


/* CM33 에서 오는 cmd 패킷을 처리한다. 명령 번호는 shared.h 의 SHARED_CMD_*. */




bool cmdPeerProcess(cmd_t *p_cmd)
{
  cmd_packet_t *p_packet = &p_cmd->packet;
  uint8_t       info[SHARED_NAME_MAX + SHARED_VER_MAX];
  uint32_t      len;


  if (p_packet->type != PKT_TYPE_CMD)
    return false;

  //-- 수신 오류(체크섬, 길이)는 그대로 돌려준다
  //
  if (p_packet->err_code != CMD_OK)
  {
    cmdSendResp(p_cmd, p_packet->cmd, p_packet->err_code, NULL, 0);
    return true;
  }

  switch (p_packet->cmd)
  {
    case SHARED_CMD_PEER_INFO:
      len = 0;
      strcpy((char *)&info[len], _DEF_BOARD_NAME);
      len += strlen(_DEF_BOARD_NAME) + 1;
      strcpy((char *)&info[len], _DEF_FIRMWATRE_VERSION);
      len += strlen(_DEF_FIRMWATRE_VERSION) + 1;
      cmdSendResp(p_cmd, p_packet->cmd, CMD_OK, info, len);
      break;

    case SHARED_CMD_ECHO:
      cmdSendResp(p_cmd, p_packet->cmd, CMD_OK, p_packet->data, p_packet->length);
      break;

    default:
      cmdSendResp(p_cmd, p_packet->cmd, ERR_CMD_NO_CMD, NULL, 0);
      break;
  }

  return true;
}

#endif
