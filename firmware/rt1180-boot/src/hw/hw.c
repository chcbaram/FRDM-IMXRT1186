#include "hw.h"
#include "clock.h"


bool hwInit(void)
{
  //-- 순서는 앱(rt1180-fw)과 같다. cliInit / logInit 은 버퍼만 잡으므로 먼저 부른다.
  //
  cliInit();
  logInit();

  uartInit();
  uartOpen(HW_UART_CH_CLI, 115200);

  logOpen(HW_LOG_CH, 115200);
  logPrintf("\r\n[ Bootloader Begin... ]\r\n");
  logPrintf("Booting..Name  \t\t: %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Booting..Ver   \t\t: %s\r\n", _DEF_FIRMWATRE_VERSION);
  logPrintf("Booting..Clock \t\t: %d MHz\r\n", (int)(SystemCoreClock / 1000000));
  logPrintf("Booting..Mode  \t\t: 0x%08X\r\n", (unsigned)SRC_GENERAL_REG->SBMR2);
  logPrintf("Booting..Run   \t\t: 0x%08X\r\n", (unsigned)SCB->VTOR);
  logPrintf("Booting..Clock Init\t: %s\r\n", clockIsApplied() ? "PLL" : "ROM default");
  logPrintf("\r\n");

  ledInit();
  qspiInit();
  rtcInit();
  resetInit();

  hwInfoInit();

  return true;
}
