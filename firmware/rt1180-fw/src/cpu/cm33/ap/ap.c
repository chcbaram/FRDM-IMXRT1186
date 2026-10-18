#include "ap.h"
#include "modules/cmd/cmd_task.h"




void apInit(void)
{
  cliOpen(HW_UART_CH_CLI, 115200);

  if (ipcIsBooted())
  {
    cmdTaskInit();
  }
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

    cliMain();
    cmdTaskUpdate();
  }
}
