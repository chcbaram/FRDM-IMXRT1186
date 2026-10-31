#include "ap.h"
#include "module.h"
#include "boot/boot.h"


static void bootUp(void);


void apInit(void)
{
  //-- 앱으로 넘어가면 여기서 돌아오지 않는다. 모듈(CLI, 나중에 cmd)은 부트로더에 머물 때만 연다
  //
  bootUp();

  moduleInit();
}

void apMain(void)
{
  uint32_t pre_time;


  logBoot(false);

  pre_time = millis();
  while (1)
  {
    //-- 부트로더는 파랑, 앱은 초록 (docs/40 4절)
    //
    if (millis() - pre_time >= 500)
    {
      pre_time = millis();
      ledToggle(_DEF_LED3);
    }

    moduleUpdate();
  }
}

/*
 * 부팅 판정 (docs/40 4절). 아래 중 하나면 부트로더에 머문다.
 *   - 앱이 부트로더를 요청했다 (reset boot → BBNSM GPR 의 MODE_BIT_BOOT)
 *   - SW3 를 두 번 눌렀다 (resetInit 의 더블클릭 판정)
 *   - 실행할 수 있는 앱이 없다 (TAG / CRC / firm_ver_t)
 * 그 밖에는 앱으로 점프한다.
 */
void bootUp(void)
{
  const char *reason;

  if (resetGetBootMode() & (1<<MODE_BIT_BOOT))
  {
    reason = "boot request";
  }
  else if (resetGetCount() >= HW_RESET_DBLCLK_CNT)
  {
    reason = "reset double click";
  }
  else
  {
    bootJumpFirm();               // 성공하면 돌아오지 않는다
    reason = "no valid app";
  }

  logPrintf("[  ] boot : stay in bootloader (%s)\n", reason);
}
