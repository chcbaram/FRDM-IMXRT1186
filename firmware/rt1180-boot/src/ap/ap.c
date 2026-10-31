#include "ap.h"




void apInit(void)
{
  cliOpen(HW_UART_CH_CLI, 115200);
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
    if (millis() - pre_time >= 100)
    {
      pre_time = millis();
      ledToggle(_DEF_LED3);
    }

    cliMain();
  }
}
