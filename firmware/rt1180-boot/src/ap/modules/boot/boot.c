#include "boot.h"
#include "module.h"
#include "util_core.h"


/* 앱(FW) 영역 레이아웃 (hw_def.h, docs/40 2절)

     FLASH_ADDR_FIRM       0x0404_0000  TAG    4 KB   firm_tag_t. 독립 섹터라 앱을 건드리지 않고 지우고 쓸 수 있다
     FLASH_ADDR_FIRM_VEC   0x0404_1000  VECTOR 1 KB   앱 벡터 테이블
     + FLASH_SIZE_VEC      0x0404_1400  VER           firm_ver_t
                                        이미지 나머지

   TAG 의 fw_crc 는 FLASH_ADDR_FIRM_VEC 부터 fw_size 바이트의 CRC-16(utilCalcCRC) 이다.
   N657X0 와 같은 형식이라 PC 도구(rt1180-fw/tools/mkfwtag.py, 43 의 download.py)를 함께 쓴다.

   앱은 QSPI XIP 로만 돈다. firm_ver_t.firm_addr 가 FLASH_ADDR_FIRM_VEC 가 아니면 점프하지 않는다. */
#define BOOT_CRC_BUF_SIZE     1024


#if CLI_USE(HW_BOOT)
static void cliBoot(cli_args_t *args);
#endif
static bool bootModuleInit(void);
static bool bootRead(uint32_t addr, void *p_data, uint32_t length);


MODULE_DEF(boot)
{
  .name     = "boot",
  .priority = MODULE_PRI_HIGH,
  .init     = bootModuleInit
};




bool bootModuleInit(void)
{
#if CLI_USE(HW_BOOT)
  cliAdd("boot", cliBoot);
#endif
  return true;
}

BootImgType_t bootVerifyFirm(void)
{
  firm_tag_t tag;
  firm_ver_t ver;
  uint8_t    buf[BOOT_CRC_BUF_SIZE];
  uint16_t   crc = 0;
  bool       has_ver;


  has_ver = bootGetVer(&ver);

  if (bootGetTag(&tag) != true)
  {
    return has_ver ? BOOT_IMG_VER : BOOT_IMG_NONE;
  }

  // 이미지가 신고한 크기와 다르면 옛 TAG 다 (앱만 새로 쓰고 TAG 는 그대로인 경우)
  if (has_ver == true && ver.firm_size > 0 && ver.firm_size != tag.fw_size)
  {
    return BOOT_IMG_VER;
  }

  for (uint32_t i = 0; i < tag.fw_size; i += sizeof(buf))
  {
    uint32_t n = tag.fw_size - i;

    if (n > sizeof(buf)) n = sizeof(buf);
    if (bootRead(FLASH_ADDR_FIRM_VEC + i, buf, n) != true)
    {
      return BOOT_IMG_NONE;
    }
    crc = utilCalcCRC(crc, buf, n);
  }

  if (crc != tag.fw_crc)
  {
    return has_ver ? BOOT_IMG_VER : BOOT_IMG_NONE;
  }
  return BOOT_IMG_TAG;
}

bool bootJumpFirm(void)
{
  firm_ver_t ver;


  if (bootVerifyFirm() != BOOT_IMG_TAG || bootGetVer(&ver) != true)
  {
    logPrintf("[E_] bootJumpFirm() - no valid image\n");
    return false;
  }

  if (ver.firm_addr != FLASH_ADDR_FIRM_VEC)
  {
    logPrintf("[E_] bootJumpFirm() - firm_addr 0x%08X ?\n", (unsigned)ver.firm_addr);
    return false;
  }

  logPrintf("[  ] jump : XIP 0x%08X  %s %s\n", (unsigned)ver.firm_addr, ver.name_str, ver.version_str);
  logPrintf("\n");

  //-- 앱이 다시 연다. 열린 채로 넘기면 앱의 uartInit 과 부딪힌다 (docs/40 5절)
  //
  uartClose(HW_UART_CH_CLI);

  bspJumpToApp(ver.firm_addr);
  return false;
}

bool bootGetTag(firm_tag_t *p_tag)
{
  if (bootRead(FLASH_ADDR_FIRM, p_tag, sizeof(firm_tag_t)) != true)
    return false;

  if (p_tag->magic_number != TAG_MAGIC_NUMBER)
    return false;

  if (p_tag->tag_crc != utilCalcCRC(0, (uint8_t *)p_tag, sizeof(firm_tag_t) - 4))
    return false;

  if (p_tag->fw_size == 0 || p_tag->fw_size > FLASH_SIZE_FIRM - FLASH_SIZE_TAG)
    return false;

  return true;
}

bool bootGetVer(firm_ver_t *p_ver)
{
  if (bootRead(FLASH_ADDR_FIRM_VEC + FLASH_SIZE_VEC, p_ver, sizeof(firm_ver_t)) != true)
    return false;

  return p_ver->magic_number == VERSION_MAGIC_NUMBER;
}

bool bootRead(uint32_t addr, void *p_data, uint32_t length)
{
  return qspiRead(addr - qspiGetAddr(), (uint8_t *)p_data, length);
}

#if CLI_USE(HW_BOOT)
void cliBoot(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    const char *img_str[] = {"NONE", "RAW", "VER (TAG 없음 · 맞지 않음)", "TAG (실행 가능)"};
    firm_tag_t  tag;
    firm_ver_t  ver;
    uint32_t    pre_time;
    BootImgType_t img;

    if (bootGetVer(&ver) == true)
    {
      cliPrintf("ver  : %s %s\n", ver.name_str, ver.version_str);
      cliPrintf("       addr 0x%08X  size %d\n", (unsigned)ver.firm_addr, (int)ver.firm_size);
    }
    else
    {
      cliPrintf("ver  : none\n");
    }

    if (bootGetTag(&tag) == true)
    {
      cliPrintf("tag  : size %d  crc 0x%04X\n", (int)tag.fw_size, (unsigned)tag.fw_crc);
    }
    else
    {
      cliPrintf("tag  : none\n");
    }

    pre_time = millis();
    img      = bootVerifyFirm();
    cliPrintf("img  : %s  (%d ms)\n", img_str[img], (int)(millis() - pre_time));
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "jump"))
  {
    bootJumpFirm();                 // 성공하면 돌아오지 않는다
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("boot info\n");
    cliPrintf("boot jump\n");
  }
}
#endif
