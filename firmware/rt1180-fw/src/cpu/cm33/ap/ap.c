#include "ap.h"


void apInit(void)
{
}

void apMain(void)
{
  uint32_t pre_time;


  //-- 1차 목표 : 녹색 LED 500 ms 토글
  //
  pre_time = millis();
  while (1)
  {
    if (millis() - pre_time >= 500)
    {
      pre_time = millis();
      ledToggle(_DEF_LED2);
    }
  }
}
