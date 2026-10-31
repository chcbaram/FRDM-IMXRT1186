#include "process/cmd_boot.h"
#include "boot/boot.h"
#include "boot_cmd.h"
#include "util_core.h"


/*
 * 부트로더 커맨드 셋 (docs/43). NUCLEO-N657X0 cmd_boot.c 를 이 보드에 맞췄다.
 * 명령 코드 · 응답은 common/hw/include/boot_cmd.h (앱과 같이 쓴다).
 *
 *   대상      영역                           쓰는 순서 (전원이 끊겨도 부팅할 수 있게)
 *   FW   0    FLASH_ADDR_FIRM (TAG + 앱)     BEGIN 에서 TAG 를 먼저 지움 -> 이미지 -> END 에서 TAG 기록 (커밋)
 *   BOOT 1    —                              아직 받지 않는다. 부트 슬롯이 하나라 쓰다 끊기면 부팅이 안 된다
 *   DATA 2    FLASH_ADDR_DATA + offset       쓰고 END 에서 CRC 를 돌려준다
 *   CM7  3    FLASH_ADDR_CM7 (RMC7 + ITCM)   쓰고 END 에서 RMC7 헤더를 확인한다. 앱이 부팅 때 헤더 CRC 를 다시 본다
 *
 * END 는 [size:4][crc:4] 를 돌려준다 (CRC-16 utilCalcCRC). 호스트가 자기 계산과 비교한다.
 *
 * 부트로더는 Code TCM 에서 돈다. QSPI 를 지우고 쓰는 동안에도 UART 수신 · SysTick 이 그대로 돈다 (docs/41).
 */
#define BOOT_READ_MAX             512
#define BOOT_BUF_SIZE             1024
#define BOOT_BLOCK_SIZE           0x10000
#define BOOT_SECTOR_SIZE          0x1000
#define BOOT_CM7_MAGIC            0x37434D52 // "RMC7" (rt1180-fw/tools/mkcm7img.py)
#define BOOT_BAUD_MIN             9600
#define BOOT_BAUD_MAX             6000000    // LPUART1 루트 24 MHz, OSR 4


static uint16_t cmdBootBegin(uint8_t *p_data, uint32_t length);
static uint16_t cmdBootErase(void);
static uint16_t cmdBootEnd(uint32_t *p_size, uint32_t *p_crc);
static uint16_t cmdBootEndFw(uint32_t *p_size, uint32_t *p_crc);
static bool     cmdBootCrc(uint32_t addr, uint32_t length, uint16_t *p_crc);
static uint32_t cmdBootWriteBase(void);
static bool     flashRead(uint32_t addr, uint8_t *p_data, uint32_t length);
static bool     flashWrite(uint32_t addr, uint8_t *p_data, uint32_t length);
static bool     flashErase(uint32_t addr, uint32_t length);

//-- FW_BEGIN ~ FW_END 사이의 전송 상태
static uint8_t  wr_target = BOOT_TARGET_FW;
static int32_t  wr_length = -1;    // 호스트가 신고한 크기. -1 이면 전송 중이 아니다
static uint32_t wr_index  = 0;     // 기록된 최대 끝 오프셋
static uint32_t wr_offset = 0;     // DATA 의 영역 안 시작 오프셋

static uint8_t  buf[BOOT_BUF_SIZE];




bool cmdBootProcess(cmd_t *p_cmd)
{
  uint16_t  cmd      = p_cmd->packet.cmd;
  uint16_t  err_code = OK;
  uint8_t  *p_data   = p_cmd->packet.data;
  uint32_t  length   = p_cmd->packet.length;


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
      firm_ver_t     fw_ver;
      firm_tag_t     tag;

      memset(&ver, 0, sizeof(ver));
      ver.img_type = (uint8_t)bootVerifyFirm();

      if (bootGetTag(&tag))
      {
        ver.fw_size = tag.fw_size;
        ver.fw_crc  = tag.fw_crc;
      }
      if (bootGetVer(&fw_ver))
      {
        //-- 플래시에서 읽은 문자열은 NUL 종료가 보장되지 않는다.
        memcpy(ver.name,    fw_ver.name_str,    sizeof(ver.name));
        memcpy(ver.version, fw_ver.version_str, sizeof(ver.version));
        ver.name[sizeof(ver.name)-1]       = 0;
        ver.version[sizeof(ver.version)-1] = 0;
      }
      cmdSendResp(p_cmd, cmd, OK, (uint8_t *)&ver, sizeof(ver));
      break;
    }

    case BOOT_CMD_FW_BEGIN:
      err_code = cmdBootBegin(p_data, length);
      cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      break;

    case BOOT_CMD_FW_ERASE:
      err_code = cmdBootErase();
      cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      break;

    case BOOT_CMD_FW_WRITE:
    {
      uint32_t offset = 0;

      if (length < 4 || wr_length <= 0)
      {
        err_code = ERR_BOOT_WRONG_CMD;
      }
      else
      {
        uint32_t n = length - 4;

        memcpy(&offset, &p_data[0], 4);

        if ((offset + n) > (uint32_t)wr_length)
        {
          err_code = ERR_BOOT_WRONG_RANGE;
        }
        else if (flashWrite(cmdBootWriteBase() + offset, &p_data[4], n) != true)
        {
          err_code = ERR_BOOT_FLASH_WRITE;
        }
        else if ((offset + n) > wr_index)
        {
          wr_index = offset + n;
        }
      }
      cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      break;
    }

    case BOOT_CMD_FW_READ:
    {
      uint32_t offset = 0;
      uint32_t len    = 0;
      uint8_t  target = BOOT_TARGET_FW;
      uint32_t base   = FLASH_ADDR_FIRM;
      uint32_t size   = FLASH_SIZE_FIRM;

      if (length < 8)
      {
        err_code = ERR_BOOT_WRONG_CMD;
      }
      else
      {
        memcpy(&offset, &p_data[0], 4);
        memcpy(&len,    &p_data[4], 4);
        if (length >= 9) target = p_data[8];

        // FW 는 TAG 부터 (weact 와 같다)
        if (target == BOOT_TARGET_BOOT) { base = FLASH_ADDR_BOOT; size = FLASH_SIZE_BOOT; }
        if (target == BOOT_TARGET_DATA) { base = FLASH_ADDR_DATA; size = FLASH_SIZE_DATA; }
        if (target == BOOT_TARGET_CM7)  { base = FLASH_ADDR_CM7;  size = FLASH_SIZE_CM7;  }

        if (target > BOOT_TARGET_CM7 || len > BOOT_READ_MAX || offset > size || len > size - offset)
          err_code = ERR_BOOT_WRONG_RANGE;
        else if (flashRead(base + offset, buf, len) != true)
          err_code = ERR_BOOT_FLASH_READ;
      }

      if (err_code == OK)
        cmdSendResp(p_cmd, cmd, OK, buf, len);
      else
        cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      break;
    }

    case BOOT_CMD_FW_END:
    {
      uint32_t resp[2] = {0, 0};

      err_code = cmdBootEnd(&resp[0], &resp[1]);
      wr_length = -1;

      if (err_code == OK)
        cmdSendResp(p_cmd, cmd, OK, (uint8_t *)resp, sizeof(resp));
      else
        cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      break;
    }

    case BOOT_CMD_FW_VERIFY:
    {
      uint8_t img = (uint8_t)bootVerifyFirm();

      if (img == BOOT_IMG_NONE) err_code = ERR_BOOT_INVALID_FW;

      cmdSendResp(p_cmd, cmd, err_code, &img, 1);
      break;
    }

    case BOOT_CMD_FW_UPDATE:
    case BOOT_CMD_FW_JUMP:
      // 부트로더에서는 앱으로 점프한다. 앱이 없으면 점프하지 않고 오류를 돌려준다
      if (bootVerifyFirm() != BOOT_IMG_TAG)
      {
        cmdSendResp(p_cmd, cmd, ERR_BOOT_INVALID_FW, NULL, 0);
      }
      else
      {
        cmdSendResp(p_cmd, cmd, OK, NULL, 0);
        delay(50);                // 응답이 나갈 시간
        bootJumpFirm();
      }
      break;

    case BOOT_CMD_BAUD:
    {
      uint32_t baud = 0;

      if (length < 4)
      {
        err_code = ERR_BOOT_WRONG_CMD;
      }
      else
      {
        memcpy(&baud, &p_data[0], 4);
        if (cmdGetBaud(p_cmd) == 0)
          err_code = ERR_BOOT_WRONG_CMD;          // 보율이 있는 채널(UART)에서만 의미가 있다
        else if (baud < BOOT_BAUD_MIN || baud > BOOT_BAUD_MAX)
          err_code = ERR_BOOT_WRONG_RANGE;
      }

      // 응답은 지금 보율로 다 보낸 뒤에 바꾼다 (uartWrite 는 전송 완료(TC)까지 기다린다)
      cmdSendResp(p_cmd, cmd, err_code, (uint8_t *)&baud, 4);
      if (err_code == OK)
      {
        cmdSetBaud(p_cmd, baud);
      }
      break;
    }

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

uint16_t cmdBootBegin(uint8_t *p_data, uint32_t length)
{
  uint32_t size   = 0;
  uint8_t  target = BOOT_TARGET_FW;
  uint32_t offset = 0;
  uint32_t max;


  if (length < 4)
    return ERR_BOOT_WRONG_CMD;

  memcpy(&size, &p_data[0], 4);
  if (length >= 5) target = p_data[4];
  if (length >= 9) memcpy(&offset, &p_data[5], 4);

  switch (target)
  {
    case BOOT_TARGET_FW:  max = FLASH_SIZE_FIRM - FLASH_SIZE_TAG; offset = 0; break;
    case BOOT_TARGET_CM7: max = FLASH_SIZE_CM7;                   offset = 0; break;
    case BOOT_TARGET_DATA:
      // 지우기가 4 KB 단위라 시작이 섹터 경계가 아니면 앞의 남의 데이터까지 지운다
      if ((offset % BOOT_SECTOR_SIZE) != 0 || offset >= FLASH_SIZE_DATA)
        return ERR_BOOT_WRONG_RANGE;
      max = FLASH_SIZE_DATA - offset;
      break;
    default:
      // BOOT : 부트 슬롯이 하나라 쓰다 끊기면 부팅할 것이 없다. probe-rs 로 쓴다 (docs/43)
      return ERR_BOOT_WRONG_RANGE;
  }

  if (size == 0 || size > max)
    return ERR_BOOT_WRONG_RANGE;

  /*
   * FW 는 TAG 섹터를 **먼저** 지운다. 전송이 중간에 끊겨도 TAG 가 무효라 옛 이미지를
   * 실행하지 않는다. TAG 가 독립 4KB 섹터라 앱 본체는 건드리지 않는다.
   */
  if (target == BOOT_TARGET_FW && flashErase(FLASH_ADDR_FIRM, FLASH_SIZE_TAG) != true)
    return ERR_BOOT_FLASH_ERASE;

  wr_target = target;
  wr_length = (int32_t)size;
  wr_index  = 0;
  wr_offset = offset;
  logPrintf("[  ] fw begin target %d, %d bytes\n", target, (int)size);
  return OK;
}

uint16_t cmdBootErase(void)
{
  uint32_t addr;
  uint32_t len;


  if (wr_length <= 0)
    return ERR_BOOT_WRONG_CMD;

  switch (wr_target)
  {
    case BOOT_TARGET_FW:
      // TAG 를 포함해 64KB 정렬 범위로 넓혀야 qspiErase() 가 블록 단위로 지운다 (지우기가 업로드의 대부분)
      addr = FLASH_ADDR_FIRM;
      len  = FLASH_SIZE_TAG + (uint32_t)wr_length;
      len  = (len + BOOT_BLOCK_SIZE - 1) & ~(BOOT_BLOCK_SIZE - 1);
      if (len > FLASH_SIZE_FIRM) len = FLASH_SIZE_FIRM;
      break;

    case BOOT_TARGET_CM7:
      // 옛 이미지의 헤더가 남지 않게 앞에서부터. 64KB 정렬
      addr = FLASH_ADDR_CM7;
      len  = ((uint32_t)wr_length + BOOT_BLOCK_SIZE - 1) & ~(BOOT_BLOCK_SIZE - 1);
      break;

    default:
      addr = FLASH_ADDR_DATA + wr_offset;
      len  = ((uint32_t)wr_length + BOOT_SECTOR_SIZE - 1) & ~(BOOT_SECTOR_SIZE - 1);
      break;
  }

  return flashErase(addr, len) ? OK : ERR_BOOT_FLASH_ERASE;
}

uint16_t cmdBootEnd(uint32_t *p_size, uint32_t *p_crc)
{
  uint16_t crc = 0;


  if (wr_length <= 0 || wr_index == 0)
    return ERR_BOOT_WRONG_CMD;

  if (wr_target == BOOT_TARGET_FW)
    return cmdBootEndFw(p_size, p_crc);

  if (wr_target == BOOT_TARGET_CM7)
  {
    uint32_t magic = 0;

    //-- 헤더가 RMC7 이 아니면 CM7 이미지가 아니다. 앱이 부팅 때 헤더의 CRC 를 다시 확인한다
    if (flashRead(FLASH_ADDR_CM7, (uint8_t *)&magic, 4) != true || magic != BOOT_CM7_MAGIC)
      return ERR_BOOT_INVALID_FW;
  }

  if (cmdBootCrc(cmdBootWriteBase(), wr_index, &crc) != true)
    return ERR_BOOT_FLASH_READ;

  *p_size = wr_index;
  *p_crc  = crc;
  logPrintf("[  ] %s end %d bytes, crc 0x%04X\n", wr_target == BOOT_TARGET_CM7 ? "cm7":"data", (int)wr_index, crc);
  return OK;
}

uint16_t cmdBootEndFw(uint32_t *p_size, uint32_t *p_crc)
{
  /*
   * TAG 를 마지막에 쓴다. 이게 커밋 마커다.
   *
   * 크기는 **`firm_ver_t.firm_size` 를 우선한다.** 호스트가 패딩을 붙이면 wr_index 가
   * 이미지 크기와 어긋나 다음 판정에서 옛 TAG 가 된다 (N657X0 · weact 에서 겪었다).
   */
  firm_tag_t tag;
  firm_ver_t ver;
  uint32_t   fw_size = wr_index;
  uint16_t   crc     = 0;


  if (bootGetVer(&ver) == true && ver.firm_size > 0 && ver.firm_size <= wr_index)
  {
    fw_size = ver.firm_size;
  }

  if (cmdBootCrc(FLASH_ADDR_FIRM_VEC, fw_size, &crc) != true)
    return ERR_BOOT_FLASH_READ;

  memset(&tag, 0, sizeof(tag));
  tag.magic_number = TAG_MAGIC_NUMBER;
  tag.fw_addr      = FLASH_SIZE_TAG;
  tag.fw_size      = fw_size;
  tag.fw_crc       = crc;
  tag.tag_crc      = utilCalcCRC(0, (uint8_t *)&tag, sizeof(tag) - 4);

  if (flashErase(FLASH_ADDR_FIRM, FLASH_SIZE_TAG) != true)
    return ERR_BOOT_FLASH_ERASE;
  if (flashWrite(FLASH_ADDR_FIRM, (uint8_t *)&tag, sizeof(tag)) != true)
    return ERR_BOOT_FLASH_WRITE;

  *p_size = fw_size;
  *p_crc  = crc;
  logPrintf("[  ] fw end %d bytes (rx %d), crc 0x%04X\n", (int)fw_size, (int)wr_index, crc);
  return OK;
}

bool cmdBootCrc(uint32_t addr, uint32_t length, uint16_t *p_crc)
{
  uint16_t crc = 0;

  for (uint32_t i = 0; i < length; i += sizeof(buf))
  {
    uint32_t n = length - i;

    if (n > sizeof(buf)) n = sizeof(buf);
    if (flashRead(addr + i, buf, n) != true)
      return false;
    crc = utilCalcCRC(crc, buf, n);
  }
  *p_crc = crc;
  return true;
}

uint32_t cmdBootWriteBase(void)
{
  if (wr_target == BOOT_TARGET_CM7)  return FLASH_ADDR_CM7;
  if (wr_target == BOOT_TARGET_DATA) return FLASH_ADDR_DATA + wr_offset;
  return FLASH_ADDR_FIRM_VEC;
}

//-- 주소는 메모리 맵 주소(0x04xx_xxxx)로 다루고 qspi 에는 오프셋으로 넘긴다
//
bool flashRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  return qspiRead(addr - qspiGetAddr(), p_data, length);
}

bool flashWrite(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  return qspiWrite(addr - qspiGetAddr(), p_data, length);
}

bool flashErase(uint32_t addr, uint32_t length)
{
  return qspiErase(addr - qspiGetAddr(), length);
}
