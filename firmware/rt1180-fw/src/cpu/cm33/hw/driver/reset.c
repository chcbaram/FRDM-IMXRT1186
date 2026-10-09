#include "reset.h"
#include "rtc.h"
#include "cli.h"
#include "log.h"
#include "led.h"

#ifdef _USE_HW_RESET
#include "fsl_device_registers.h"

/*
 * 리셋 원인과 부트 모드 — stm32h563-core 의 reset.c 를 옮겼다.
 *
 * 차이점
 *   - 리셋 원인 : RCC 플래그 대신 SRC_GENERAL_REG->SRSR (RM 27.6.1.9).
 *                 POR 에서만 지워지고 나머지는 쌓이므로 읽은 뒤 1 을 써서 지운다.
 *   - 백업 레지스터 : RTC 백업 레지스터 대신 BBNSM GPR[0..7] (rtc.c)
 *   - ECC 주소 : 32 bit 주소를 그대로 두고, 유효 표시는 따로 GPR 하나를 쓴다.
 *
 * SRSR → RESET_BIT 분류
 *   POWER  POR_RST (bit0)                       전원 인가
 *   PIN    IPP_POR_B (bit16)                    SW2, MCU-Link 리셋 (J24, J26 → POR_B)
 *   SOFT   CM33_REQUEST, CM7_REQUEST, JTAG_SW   SYSRESETREQ, 디버거
 *   WDG    WDOG1~5
 *   ETC    lockup, EdgeLock, 온도센서, DCDC 과전압, ECAT
 */
#define SRSR_POWER   (SRC_GENERAL_SRSR_POR_RST_MASK)
#define SRSR_PIN     (SRC_GENERAL_SRSR_IPP_POR_B_MASK)
#define SRSR_SOFT    (SRC_GENERAL_SRSR_CM33_REQUEST_MASK | SRC_GENERAL_SRSR_CM7_REQUEST_MASK | \
                      SRC_GENERAL_SRSR_JTAG_SW_RST_MASK)
#define SRSR_WDG     (SRC_GENERAL_SRSR_WDOG1_RST_B_MASK | SRC_GENERAL_SRSR_WDOG2_RST_B_MASK | \
                      SRC_GENERAL_SRSR_WDOG3_RST_B_MASK | SRC_GENERAL_SRSR_WDOG4_RST_B_MASK | \
                      SRC_GENERAL_SRSR_WDOG5_RST_B_MASK)
#define SRSR_ETC     (SRC_GENERAL_SRSR_CM33_LOCKUP_MASK | SRC_GENERAL_SRSR_CM7_LOCKUP_MASK | \
                      SRC_GENERAL_SRSR_EDGELOCK_RESET_B_MASK | SRC_GENERAL_SRSR_TEMPSENSE_RST_B_MASK | \
                      SRC_GENERAL_SRSR_DCDC_OVVT_MASK | SRC_GENERAL_SRSR_ECAT_RSTO_MASK)


// 백업 레지스터 카운터 = 매직(상위 16비트) | 값. 전원이 끊긴 뒤의 쓰레기 값을 걸러낸다
#define RESET_CNT_MAGIC       0xA55A0000UL
#define RESET_CNT_MASK        0x000000FFUL
// ECC 오류 주소 : HW_RTC_ECC_ADDR 에 주소 그대로, HW_RTC_ECC_VALID 에 매직
#define RESET_ECC_MAGIC       0xECC0ECC0UL


#if CLI_USE(HW_RESET)
static void cliReset(cli_args_t *args);
#endif

#if defined(HW_RESET_BOOT) && HW_RESET_BOOT > 0
static uint32_t resetCntLoad(void);
static void     resetCntSave(uint32_t cnt);
#endif


static bool     is_init     = false;
static uint32_t reset_bits  = 0;
static uint32_t boot_mode   = 0;
static uint32_t reset_count = 0;
static uint32_t reset_srsr  = 0;            // 이번 부팅의 SRSR 원본 (reset info)


static const char *reset_bit_str[RESET_BIT_MAX] =
  {
    "RESET_BIT_POWER",
    "RESET_BIT_PIN",
    "RESET_BIT_WDG",
    "RESET_BIT_SOFT",
    "RESET_BIT_ETC",
  };

static const char *mode_bit_str[MODE_BIT_MAX] =
  {
    "MODE_BIT_BOOT",
    "MODE_BIT_UPDATE",
  };



bool resetInit(void)
{
  bool ret;


#if defined(HW_RESET_BOOT) && HW_RESET_BOOT > 0
  reset_srsr = SRC_GENERAL_REG->SRSR;

  if (reset_srsr & SRSR_PIN)   reset_bits |= (1<<RESET_BIT_PIN);
  if (reset_srsr & SRSR_POWER) reset_bits |= (1<<RESET_BIT_POWER);
  if (reset_srsr & SRSR_WDG)   reset_bits |= (1<<RESET_BIT_WDG);
  if (reset_srsr & SRSR_SOFT)  reset_bits |= (1<<RESET_BIT_SOFT);
  if (reset_srsr & SRSR_ETC)   reset_bits |= (1<<RESET_BIT_ETC);

  //-- 쓴 비트만 지워진다 (W1C). 지우지 않으면 다음 리셋 원인과 섞인다.
  //
  SRC_GENERAL_REG->SRSR = reset_srsr;

  rtcSetReg(HW_RTC_RESET_BITS, reset_bits);
#else
  rtcGetReg(HW_RTC_RESET_BITS, &reset_bits);
#endif

  rtcGetReg(HW_RTC_BOOT_MODE, &boot_mode);
  rtcSetReg(HW_RTC_BOOT_MODE, 0);


#if defined(HW_RESET_BOOT) && HW_RESET_BOOT > 0
  //-- 리셋 버튼 더블클릭 감지
  //
  //   POWER 를 가장 먼저 검사한다. 전원 인가 때 POR_B 핀도 함께 올라오면
  //   PIN 비트가 같이 세트될 수 있다. 순서를 뒤집으면 "전원 껐다 켜기 2회" 로도
  //   부트로더에 들어간다. (STM32H5 에서 실제로 겪었다. 이 칩은 docs/24 에서 실측)
  //
  {
    uint32_t cnt = resetCntLoad();

    //   SOFT/WDG 도 PIN 보다 먼저 거른다. STM32H5 는 소프트 리셋이 NRST 핀으로
    //   전파되어 PIN 이 같이 세트됐다. 이 칩도 같은지 docs/24 에서 실측했다.
    //
    if (reset_bits & (1<<RESET_BIT_POWER))
    {
      cnt = 0;                    // 전원 인가(BOR)는 항상 새 시작
    }
    else if (reset_bits & ((1<<RESET_BIT_SOFT) | (1<<RESET_BIT_WDG)))
    {
      cnt = 0;                    // 소프트/워치독 리셋은 집계하지 않는다
    }
    else if (reset_bits & (1<<RESET_BIT_PIN))
    {
      cnt++;                      // 순수 NRST 버튼만 집계
    }
    else
    {
      cnt = 0;
    }

    if (cnt > HW_RESET_DBLCLK_CNT)
      cnt = HW_RESET_DBLCLK_CNT;

    reset_count = cnt;

    // 두 번째 클릭을 받는 창. NRST 를 실제로 누른 경우에만 지연이 생기고
    // 전원 인가 부팅(POR)에서는 지연이 0 이다.
    //
    if (cnt == 1)
    {
      resetCntSave(cnt);
      ledOn(_DEF_LED1);
      delay(HW_RESET_DBLCLK_MS);
      ledOff(_DEF_LED1);
    }
    resetCntSave(0);
  }
#endif


  logPrintf("[OK] resetInit()\n");
  logPrintf("     SRSR : 0x%08X\n", (unsigned)reset_srsr);
  for (int i=0; i<RESET_BIT_MAX; i++)
  {
    if (reset_bits & (1<<i))
    {
      logPrintf("     %s\n", reset_bit_str[i]);
    }
  }
  for (int i=0; i<MODE_BIT_MAX; i++)
  {
    if (boot_mode & (1<<i))
    {
      logPrintf("     %s\n", mode_bit_str[i]);
    }
  }
  logPrintf("     reset_count : %d\n", (int)reset_count);

  is_init = true;
#if CLI_USE(HW_RESET)
  cliAdd("reset", cliReset);
#endif

  ret = is_init;
  return ret;
}

void resetLog(void)
{
}

void resetToBoot(void)
{
  resetSetBootMode(1<<MODE_BIT_BOOT);
  resetToReset();
}

void resetToUpdate(void)
{
  resetSetBootMode(1<<MODE_BIT_UPDATE);
  resetToReset();
}

void resetToReset(void)
{
  NVIC_SystemReset();
}

uint32_t resetGetBits(void)
{
  return reset_bits;
}

void resetSetBits(uint32_t data)
{
  reset_bits = data;
}

void resetSetBootMode(uint32_t data)
{
  boot_mode = data;
  rtcSetReg(HW_RTC_BOOT_MODE, data);
}

uint32_t resetGetBootMode(void)
{
  return boot_mode;
}

uint32_t resetGetCount(void)
{
  return reset_count;
}


//-- 부팅 확인 카운터
//
//   부트로더가 앱으로 점프하기 직전에 증가시키고, 앱이 일정 시간 정상 동작한 뒤
//   resetConfirmBoot() 으로 0 으로 되돌린다. 연속 미확인이 HW_BOOT_TRY_MAX 에
//   도달하면 이전 이미지로 롤백한다.
//
uint32_t resetGetBootTry(void)
{
  uint32_t reg = 0;

  rtcGetReg(HW_RTC_BOOT_TRY, &reg);
  if ((reg & 0xFFFF0000UL) != RESET_CNT_MAGIC)
    return 0;

  return reg & RESET_CNT_MASK;
}

void resetSetBootTry(uint32_t cnt)
{
  rtcSetReg(HW_RTC_BOOT_TRY, RESET_CNT_MAGIC | (cnt & RESET_CNT_MASK));
}

//-- 폴트 리셋 카운터
//
//   fault.c 의 faultReset() 이 NVIC_SystemReset() 직전에 증가시킨다.
//   .noinit(SRAM) 은 전원이 끊기면 사라지므로 백업 레지스터에 둔다.
//
uint32_t resetGetFaultCount(void)
{
  uint32_t reg = 0;

  rtcGetReg(HW_RTC_FAULT_CNT, &reg);
  if ((reg & 0xFFFF0000UL) != RESET_CNT_MAGIC)
    return 0;

  return reg & RESET_CNT_MASK;
}

void resetIncFaultCount(void)
{
  uint32_t cnt = resetGetFaultCount();

  if (cnt < RESET_CNT_MASK)
    cnt++;

  rtcSetReg(HW_RTC_FAULT_CNT, RESET_CNT_MAGIC | cnt);
}

void resetConfirmBoot(void)
{
  resetSetBootTry(0);
  rtcSetReg(HW_RTC_FAULT_CNT, RESET_CNT_MAGIC | 0);
}

bool resetGetEccAddr(uint32_t *p_addr)
{
  uint32_t reg = 0;

  rtcGetReg(HW_RTC_ECC_VALID, &reg);
  if (reg != RESET_ECC_MAGIC)
    return false;

  rtcGetReg(HW_RTC_ECC_ADDR, p_addr);
  return true;
}

void resetClearEccAddr(void)
{
  rtcSetReg(HW_RTC_ECC_ADDR, 0);
  rtcSetReg(HW_RTC_ECC_VALID, 0);
}


#if defined(HW_RESET_BOOT) && HW_RESET_BOOT > 0
//-- 카운트 저장/로드를 분리해 둔다.
//   RTC/LSE 에 문제가 생기면 .noinit SRAM 방식으로 즉시 바꿔 끼울 수 있다.
//   더블클릭 판정은 부트로더만 하므로 앱 빌드에서는 통째로 빠진다.
//
uint32_t resetCntLoad(void)
{
  uint32_t reg = 0;

  rtcGetReg(HW_RTC_RESET_CNT, &reg);

  // VBAT 이 없는 보드는 전원이 끊기면 백업 도메인이 날아가 부정값이 된다.
  // 매직으로 유효성을 판정한다.
  //
  if ((reg & 0xFFFF0000UL) != RESET_CNT_MAGIC)
    return 0;

  return reg & RESET_CNT_MASK;
}

void resetCntSave(uint32_t cnt)
{
  rtcSetReg(HW_RTC_RESET_CNT, RESET_CNT_MAGIC | (cnt & RESET_CNT_MASK));
}
#endif


#if CLI_USE(HW_RESET)
void cliReset(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    uint32_t ecc_addr;

    cliPrintf("SRSR        : 0x%08X\n", (unsigned)reset_srsr);
    cliPrintf("Reset Bits  : 0x%X\n", (unsigned)reset_bits);
    for (int i=0; i<RESET_BIT_MAX; i++)
    {
      if (reset_bits & (1<<i))
      {
        cliPrintf("      %s\n", reset_bit_str[i]);
      }
    }
    cliPrintf("Boot Mode   : 0x%X\n", (unsigned)boot_mode);
    for (int i=0; i<MODE_BIT_MAX; i++)
    {
      if (boot_mode & (1<<i))
      {
        cliPrintf("      %s\n", mode_bit_str[i]);
      }
    }
    cliPrintf("reset count : %d\n", (int)reset_count);
    cliPrintf("boot try    : %d\n", (int)resetGetBootTry());
    cliPrintf("fault count : %d\n", (int)resetGetFaultCount());
    if (resetGetEccAddr(&ecc_addr))
      cliPrintf("ecc addr    : 0x%08X\n", (unsigned)ecc_addr);
    else
      cliPrintf("ecc addr    : -\n");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "boot"))
  {
    resetToBoot();
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "update"))
  {
    resetToUpdate();
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "reset"))
  {
    resetToReset();
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "fault"))
  {
    if (args->isStr(1, "inc"))
    {
      resetIncFaultCount();
      cliPrintf("fault count : %d\n", (int)resetGetFaultCount());
      ret = true;
    }
    if (args->isStr(1, "clear"))
    {
      resetConfirmBoot();
      cliPrintf("cleared\n");
      ret = true;
    }
  }

  if (ret == false)
  {
    cliPrintf("reset info\n");
    cliPrintf("reset boot\n");
    cliPrintf("reset update\n");
    cliPrintf("reset reset\n");
    cliPrintf("reset fault inc|clear\n");
  }
}
#endif


#endif
