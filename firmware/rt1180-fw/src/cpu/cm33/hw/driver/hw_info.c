#include "hw_info.h"


#ifdef _USE_HW_INFO
#include "cli.h"
#include "clock.h"
#include "fsl_clock.h"

/*
 * 보드/칩 상태를 읽어 보여 주는 CLI. 어느 드라이버에도 속하지 않는 읽기 전용 조회만 둔다.
 *
 *   hwinfo boot  : 부트 모드, 리셋 원인, BOOT_CFG 퓨즈 (docs/01-boot-sequence.md)
 *   hwinfo clock : PLL, 클럭 루트 계산값과 측정값 (docs/22-clock.md)
 *
 * 명령을 hwinfo 아래에 둔다. boot / clock 이라는 이름은 나중에 생길 드라이버
 * (부트로더 boot.c, 클럭 드라이버 등)가 쓰도록 비워 둔다. 그런 드라이버가 생기면
 * 해당 항목은 그쪽으로 옮긴다.
 */


#if CLI_USE(HW_INFO)
static void cliHwInfo(cli_args_t *args);
static void hwInfoShowBoot(void);
static void hwInfoShowClock(void);
static const char *bootModeStr(uint32_t mode);
#endif




bool hwInfoInit(void)
{
#if CLI_USE(HW_INFO)
  cliAdd("hwinfo", cliHwInfo);
#endif
  return true;
}



#if CLI_USE(HW_INFO)
/*
 * 부팅 관련 상태를 실행 중에 읽는다. docs/01-boot-sequence.md 의 표와 같은 값이다.
 */
static void hwInfoShowBoot(void)
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
}

/*
 * 클럭 상태. calc 는 CCM 레지스터로 계산한 값, meas 는 CCM OBSERVE 가
 * 하드웨어로 센 값이다. 둘이 다르면 계산의 전제(PLL 설정 등)가 틀린 것이다.
 */
static void hwInfoShowClock(void)
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
}

static const char *bootModeStr(uint32_t mode)
{
  static const char *str[8] =
  {
    "Internal Fuses", "Serial Downloader", "eMMC (uSDHC2)", "SD (uSDHC1)",
    "FlexSPI NOR", "FlexSPI NAND 2K", "Infinite Loop", "Test",
  };
  return str[mode & 0x07];
}
#endif


#if CLI_USE(HW_INFO)
void cliHwInfo(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "boot"))
  {
    hwInfoShowBoot();
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "clock"))
  {
    hwInfoShowClock();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("hwinfo boot\n");
    cliPrintf("hwinfo clock\n");
  }
}
#endif

#endif
