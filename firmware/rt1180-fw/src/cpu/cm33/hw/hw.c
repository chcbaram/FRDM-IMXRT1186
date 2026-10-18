#include "hw.h"
#include "fsl_clock.h"
#include "clock.h"


bool hwInit(void)
{
  //-- 순서가 중요하다. (titan-mini 와 같다)
  //   cliInit / logInit 은 버퍼만 잡으므로 먼저 부른다. 그래야 이후 드라이버가
  //   cliAdd() 로 자기 명령을 등록할 수 있고, logPrintf() 가 열리기 전 출력을
  //   부트 버퍼에 모아 둘 수 있다.
  //
  cliInit();
  logInit();

  swtimerInit();
  uartInit();

  uartOpen(HW_UART_CH_CLI, 115200);

  logOpen(HW_LOG_CH, 115200);
  logPrintf("\r\n[ Firmware Begin... ]\r\n");
  logPrintf("Booting..Name  \t\t: %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Booting..Ver   \t\t: %s\r\n", _DEF_FIRMWATRE_VERSION);
  logPrintf("Booting..Clock \t\t: %d MHz\r\n", (int)(SystemCoreClock / 1000000));
  logPrintf("Booting..Mode  \t\t: 0x%08X\r\n", (unsigned)SRC_GENERAL_REG->SBMR2);
  logPrintf("Booting..Clock Init\t: %s\r\n", clockIsApplied() ? "PLL" : "ROM default");
  logPrintf("\r\n");

  //-- LED 는 pwm 채널을 쓰므로 pwm 다음에 둔다. 둘 다 로그가 열린 뒤에 해야
  //   pwmInit 결과가 콘솔에 남는다.
  //
  pwmInit();
  ledInit();
  qspiInit();
  rtcInit();
  resetInit();
  buttonInit();

  //-- CM7 기동은 로그가 열린 뒤에 한다. 시도와 결과를 남기기 위해서다. (titan-mini 와 같다)
  //
  ipcInit();
  if (ipcIsBooted())
  {
    logPrintf("[OK] CM7 %s (%d ms)\n", ipcGetStateStr(), (int)ipcGetBootTime());
    logPrintf("     name  : %s\n", ipcGetName());
    logPrintf("     ver   : %s\n", ipcGetVersion());
    logPrintf("     clock : %d MHz\n", (int)(ipcGetClock() / 1000000));
  }
  else
  {
    logPrintf("[%s] CM7 %s\n", ipcGetState() == IPC_STATE_DISABLED ? "  " : "E_", ipcGetStateStr());
  }

  hwInfoInit();

  return true;
}

