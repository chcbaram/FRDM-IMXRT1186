#include "process/cmd_boot.h"
#include "boot_cmd.h"


/*
 * 앱 쪽 부트 명령 (docs/43). download.py 가 앱이 돌고 있는 보드에 붙었을 때 쓴다.
 *
 *   INFO       mode = APP 로 응답한다. 호스트는 이것을 보고 FW_UPDATE 를 보낸다
 *   VERSION    이 앱의 firm_ver_t
 *   FW_UPDATE  응답 뒤 resetToBoot() — 부트로더에 머물게 하고 리셋한다
 *   RESET      응답 뒤 리셋
 *   그 밖      ERR_BOOT_WRONG_CMD. 앱은 플래시를 쓰지 않는다 (docs/26 7절, N657X0 와 같다)
 */
extern volatile const firm_ver_t firm_ver;




bool cmdBootProcess(cmd_t *p_cmd)
{
  uint16_t cmd = p_cmd->packet.cmd;


  switch (cmd)
  {
    case BOOT_CMD_INFO:
    {
      boot_info_t info;

      memset(&info, 0, sizeof(info));
      info.magic         = MAGIC_NUMBER;
      info.mode          = HW_DEV_MODE;
      info.boot_addr     = FLASH_ADDR_BOOT;
      info.boot_size     = FLASH_SIZE_BOOT;
      info.firm_addr     = FLASH_ADDR_FIRM;
      info.firm_vec_addr = FLASH_ADDR_FIRM_VEC;
      info.firm_size     = FLASH_SIZE_FIRM;
      info.tag_size      = FLASH_SIZE_TAG;
      info.max_fw_size   = FLASH_SIZE_FIRM - FLASH_SIZE_TAG;
      info.cmd_ver       = BOOT_CMD_VER;
      info.data_addr     = FLASH_ADDR_DATA;
      info.data_size     = FLASH_SIZE_DATA;
      info.baud          = cmdGetBaud(p_cmd);
      info.cm7_addr      = FLASH_ADDR_CM7;
      info.cm7_size      = FLASH_SIZE_CM7;
      snprintf(info.name,    sizeof(info.name),    "%s", _DEF_BOARD_NAME);
      snprintf(info.version, sizeof(info.version), "%s", _DEF_FIRMWATRE_VERSION);

      cmdSendResp(p_cmd, cmd, OK, (uint8_t *)&info, sizeof(info));
      break;
    }

    case BOOT_CMD_VERSION:
    {
      boot_version_t ver;

      memset(&ver, 0, sizeof(ver));
      ver.img_type = 3;                       // 돌고 있으므로 TAG 까지 맞았다 (BOOT_IMG_TAG)
      ver.fw_size  = firm_ver.firm_size;
      snprintf(ver.name,    sizeof(ver.name),    "%s", _DEF_BOARD_NAME);
      snprintf(ver.version, sizeof(ver.version), "%s", _DEF_FIRMWATRE_VERSION);
      cmdSendResp(p_cmd, cmd, OK, (uint8_t *)&ver, sizeof(ver));
      break;
    }

    case BOOT_CMD_FW_UPDATE:
      cmdSendResp(p_cmd, cmd, OK, NULL, 0);
      delay(50);                              // 응답이 나갈 시간
      resetToBoot();
      break;

    case BOOT_CMD_RESET:
      cmdSendResp(p_cmd, cmd, OK, NULL, 0);
      delay(50);
      resetToReset();
      break;

    default:
      cmdSendResp(p_cmd, cmd, ERR_BOOT_WRONG_CMD, NULL, 0);
      break;
  }

  return true;
}
