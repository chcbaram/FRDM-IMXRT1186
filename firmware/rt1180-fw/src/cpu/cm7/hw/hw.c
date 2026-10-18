#include "hw.h"


bool hwInit(void)
{
  //-- 로그는 버퍼에만 쌓인다. CM33 의 `peer log` 로 본다 (hw/driver/log.c).
  //
  logInit();
  threadInit();

  logPrintf("[ CM7 Firmware Begin... ]\r\n");
  logPrintf("Booting..Name  \t\t: %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Booting..Ver   \t\t: %s\r\n", _DEF_FIRMWATRE_VERSION);
  logPrintf("Booting..Clock \t\t: %d MHz\r\n", (int)(SystemCoreClock / 1000000));

  logPrintf("[%s] psramInit()\r\n", psramInit() ? "OK" : "E_");

  //-- 공유 블록을 잡고 CM33 에 살아 있음을 알린다.
  //   magic 이 여기서 서면 CM33 의 기동 대기가 풀린다.
  //
  ipcInit();

  return true;
}
