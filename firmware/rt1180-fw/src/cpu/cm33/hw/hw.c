#include "hw.h"
#include "fsl_clock.h"


#if CLI_USE(HW_BOOT)
static void cliBoot(cli_args_t *args);
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

  ledInit();
  uartInit();

  uartOpen(HW_UART_CH_CLI, 115200);

  logOpen(HW_LOG_CH, 115200);
  logPrintf("\r\n[ Firmware Begin... ]\r\n");
  logPrintf("Booting..Name  \t\t: %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Booting..Ver   \t\t: %s\r\n", _DEF_FIRMWATRE_VERSION);
  logPrintf("Booting..Clock \t\t: %d MHz\r\n", (int)(SystemCoreClock / 1000000));
  logPrintf("Booting..Mode  \t\t: 0x%08X\r\n", (unsigned)SRC_GENERAL_REG->SBMR2);
  logPrintf("\r\n");

#if CLI_USE(HW_BOOT)
  cliAdd("boot", cliBoot);
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
