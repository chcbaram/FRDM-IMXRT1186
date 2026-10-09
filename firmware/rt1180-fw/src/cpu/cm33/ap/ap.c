#include "ap.h"


static button_input_t button_input;


void apInit(void)
{
  buttonInputInit(&button_input);
}

void apMain(void)
{
  uint32_t pre_time;


  pre_time = millis();
  while (1)
  {
    if (millis() - pre_time >= 500)
    {
      pre_time = millis();
      ledToggle(_DEF_LED2);
    }

    //-- SW4 : 누를 때마다 파란 LED 토글, 1초 이상 누르면 빨간 LED 토글
    //
    buttonInputUpdate(&button_input);
    if (buttonInputGetPressed(&button_input, _DEF_BUTTON1))
    {
      ledToggle(_DEF_LED3);
    }
    if (buttonInputGetHold(&button_input, _DEF_BUTTON1, 1000))
    {
      ledToggle(_DEF_LED1);
    }

    cliMain();
  }
}
