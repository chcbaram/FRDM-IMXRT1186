#include "hw.h"
#include "fsl_clock.h"
#include "clock.h"


#if CLI_USE(HW_BOOT)
static void cliBoot(cli_args_t *args);
#endif
#if CLI_USE(HW_CLOCK)
static void cliClock(cli_args_t *args);
#endif




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
  ledInit();
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

#if CLI_USE(HW_BOOT)
  cliAdd("boot", cliBoot);
#endif
#if CLI_USE(HW_CLOCK)
  cliAdd("clock", cliClock);
#endif

  cliOpen(HW_UART_CH_CLI, 115200);

  return true;
}


#if CLI_USE(HW_BOOT)
/*
 * 부팅 관련 상태를 실행 중에 읽는다. docs/01-boot-sequence.md 의 표와 같은 값이다.
 */
static const char *bootModeStr(uint32_t mode)
{
  static const char *str[8] =
  {
    "Internal Fuses", "Serial Downloader", "eMMC (uSDHC2)", "SD (uSDHC1)",
    "FlexSPI NOR", "FlexSPI NAND 2K", "Infinite Loop", "Test",
  };
  return str[mode & 0x07];
}

void cliBoot(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    uint32_t sbmr2 = SRC_GENERAL_REG->SBMR2;
    uint32_t mode  = (sbmr2 >> 24) & 0x3F;

    cliPrintf("SBMR2        : 0x%08X\n", (unsigned)sbmr2);
    cliPrintf("  BOOT_MODE  : %d%d%d  %s\n",
              (int)((mode >> 2) & 1), (int)((mode >> 1) & 1), (int)(mode & 1), bootModeStr(mode));
    cliPrintf("  boot core  : %s\n", (mode & 0x08) ? "CM33" : "-");
    cliPrintf("SRSR (reset) : 0x%08X\n", (unsigned)SRC_GENERAL_REG->SRSR);
    cliPrintf("GPR9 stage   : 0x%04X\n", (unsigned)(SRC_GENERAL_REG->GPR[9] & 0xFFFF));
    cliPrintf("\n");

    //-- 퓨즈 워드 24~35 = BOOT_CFG0~11 (RM 26.2)
    //
    for (int i = 0; i < 12; i++)
    {
      cliPrintf("BOOT_CFG%-2d   : 0x%08X\n", i, (unsigned)OCOTP_FSB->OTP_SHADOW_PARTA[24 + i]);
    }
    uint32_t cfg2 = OCOTP_FSB->OTP_SHADOW_PARTA[26];
    uint32_t cfg7 = OCOTP_FSB->OTP_SHADOW_PARTA[31];
    cliPrintf("  XSPI_INSTANCE : FlexSPI%d\n", (int)(((cfg2 >> 7) & 1) + 1));
    cliPrintf("  BOOT_FREQ     : %s\n", ((cfg7 >> 19) & 1) ? "PLL3 (240 MHz)" : "RCOSC200M (200 MHz)");
    cliPrintf("\n");

    cliPrintf("Clock M33     : %d Hz\n", (int)CLOCK_GetRootClockFreq(kCLOCK_Root_M33));
    cliPrintf("Clock FlexSPI2: %d Hz\n", (int)CLOCK_GetRootClockFreq(kCLOCK_Root_Flexspi2));
    cliPrintf("Clock LPUART1 : %d Hz\n", (int)CLOCK_GetRootClockFreq(kCLOCK_Root_Lpuart0102));
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("boot info\n");
  }
}
#endif


#if CLI_USE(HW_CLOCK)
/*
 * 클럭 상태. calc 는 CCM 레지스터로 계산한 값, meas 는 CCM OBSERVE 가
 * 하드웨어로 센 값이다. 둘이 다르면 계산의 전제(PLL 설정 등)가 틀린 것이다.
 */
void cliClock(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    const char *name;
    bool        en, bypass;
    uint32_t    hz;

    cliPrintf("OSC 24M      : %s\n", clockIsOsc24mOn() ? "on (stable)" : "off");
    cliPrintf("init         : %s\n", clockIsApplied() ? "PLL (bsp/clock.c)" : "ROM default");
    cliPrintf("\n");

    for (uint32_t i = 0; i < clockGetPllCount(); i++)
    {
      clockGetPllInfo(i, &name, &en, &bypass, &hz);
      cliPrintf("%-10s : %-3s %-7s %10d Hz\n", name, en ? "on" : "off", bypass ? "bypass" : "", (int)hz);
    }
    cliPrintf("\n");

    cliPrintf("%-10s   mux div %12s %12s\n", "root", "calc Hz", "meas Hz");
    for (uint32_t i = 0; i < clockGetRootCount(); i++)
    {
      clock_info_t info;

      clockGetRootInfo(i, &info);
      cliPrintf("%-10s : %3d %3d %12d %12d\n",
                info.name, (int)info.mux, (int)info.div, (int)info.calc_hz, (int)info.meas_hz);
    }
    cliPrintf("\nSystemCoreClock : %d Hz\n", (int)SystemCoreClock);
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("clock info\n");
  }
}
#endif
