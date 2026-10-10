#include "ap.h"


void apInit(void)
{
}

void apMain(void)
{
  //-- 지금은 살아 있다는 표시만 남긴다. LED 와 UART 는 CM33 이 소유한다.
  //
  while (1)
  {
    ipcUpdate();
    delay(10);
  }
}
