#include "ap.h"
#include "modules/cmd/cmd_task.h"


void apInit(void)
{
  cmdTaskInit();
}

void apMain(void)
{
  uint32_t pre_ms = millis();


  //-- 살아 있다는 표시(10 ms)와 CM33 에서 오는 cmd 패킷 처리. LED 와 UART 는 CM33 이 소유한다.
  //
  while (1)
  {
    if (millis() - pre_ms >= 10)
    {
      pre_ms = millis();
      ipcUpdate();
    }
    cmdTaskUpdate();
  }
}
