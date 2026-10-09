#include "rtc.h"
#include "log.h"
#include "cli.h"
#include <time.h>


#ifdef _USE_HW_RTC
#include "fsl_device_registers.h"

/*
 * RTC — BBNSM (Battery-Backed Non-Secure Module), RM 25장
 *
 *   카운터 : RTC_MS[14:0] : RTC_LS[31:0] = 47 bit, 32.768 kHz (Y1) 틱
 *            초 = (MS << 17) | (LS >> 15)
 *   GPR    : GPR[0..7] 32 bit x 8. BBSM 도메인이라 POR_B / 시스템 리셋에도 남는다.
 *            STM32 의 RTC 백업 레지스터 자리. reset.c 가 쓴다.
 *
 * 전원: VDD_BBSM 은 3V3 에서 J10(기본 단락)으로 받는다. 코인 배터리 J11 은 DNP 라
 *       보드 전원을 끄면 시각과 GPR 이 지워진다.
 *
 * 시각은 유닉스 시간(1970-01-01 UTC 기준 초)으로 카운터에 넣는다.
 *
 * 주의: BBNSM_CTRL 에는 TOSP(Turn Off System Power) 비트가 있다. 이 보드는
 *       DP_EN=1(Dumb PMIC) 상태로 부팅하므로 TOSP 를 세우면 전원이 꺼진다.
 *       CTRL 은 반드시 필요한 필드만 읽고-고쳐-쓴다. SDK BBNSM_RTC_Init() 은
 *       보정을 켜면 CTRL 을 통째로 덮어써서 쓰지 않는다.
 */

#define RTC_EN_DISABLE        0x1U        // BBNSM_CTRL[1:0]
#define RTC_EN_ENABLE         0x2U
#define RTC_TIME_SET_MIN      1704067200UL  // 2024-01-01 00:00:00 UTC. 이보다 작으면 시각이 안 맞춰진 것


static bool is_init = false;


#if CLI_USE(HW_RTC)
static void cliCmd(cli_args_t *args);
#endif




static void rtcSetEnable(bool enable)
{
  uint32_t ctrl = BBNSM->BBNSM_CTRL;

  ctrl &= ~(BBNSM_BBNSM_CTRL_RTC_EN_MASK | BBNSM_BBNSM_CTRL_TOSP_MASK);
  ctrl |= BBNSM_BBNSM_CTRL_RTC_EN(enable ? RTC_EN_ENABLE : RTC_EN_DISABLE);
  BBNSM->BBNSM_CTRL = ctrl;
}

static bool rtcIsEnabled(void)
{
  return ((BBNSM->BBNSM_CTRL & BBNSM_BBNSM_CTRL_RTC_EN_MASK) >> BBNSM_BBNSM_CTRL_RTC_EN_SHIFT) == RTC_EN_ENABLE;
}

static uint32_t rtcGetSeconds(void)
{
  uint32_t seconds;
  uint32_t tmp = 0;

  //-- 두 레지스터를 따로 읽으므로 LS 가 넘어가는 순간에 걸릴 수 있다.
  //   같은 값이 두 번 나올 때까지 읽는다. (SDK BBNSM_RTC_GetSeconds 와 같다)
  //
  do
  {
    seconds = tmp;
    tmp     = (BBNSM->BBNSM_RTC_MS << 17U) | (BBNSM->BBNSM_RTC_LS >> 15U);
  } while (tmp != seconds);

  return seconds;
}

static void rtcSetSeconds(uint32_t seconds)
{
  bool enabled = rtcIsEnabled();

  rtcSetEnable(false);
  BBNSM->BBNSM_RTC_MS = seconds >> 17U;
  BBNSM->BBNSM_RTC_LS = seconds << 15U;
  if (enabled)
  {
    rtcSetEnable(true);
  }
}

bool rtcInit(void)
{
  //-- ROM 은 RTC 를 켜지 않는다 (부팅 직후 CTRL = 0x0100_0005, RTC_EN=01).
  //   BBSM 도메인이라 한 번 켜면 전원이 있는 동안 계속 돈다.
  //
  if (!rtcIsEnabled())
  {
    rtcSetEnable(true);
  }

  is_init = rtcIsEnabled();

  logPrintf("[%s] rtcInit()\n", is_init ? "OK":"NG");

#if CLI_USE(HW_RTC)
  cliAdd("rtc", cliCmd);
#endif
  return is_init;
}

bool rtcIsInit(void)
{
  return is_init;
}

bool rtcIsTimeSet(void)
{
  if (is_init != true)
    return false;

  return rtcGetSeconds() >= RTC_TIME_SET_MIN;
}

bool rtcGetInfo(rtc_info_t *rtc_info)
{
  time_t    t = (time_t)rtcGetSeconds();
  struct tm tm;

  if (is_init != true)
    return false;

  gmtime_r(&t, &tm);

  rtc_info->time.hours   = tm.tm_hour;
  rtc_info->time.minutes = tm.tm_min;
  rtc_info->time.seconds = tm.tm_sec;

  rtc_info->date.year  = (tm.tm_year + 1900) % 100;
  rtc_info->date.month = tm.tm_mon + 1;
  rtc_info->date.day   = tm.tm_mday;
  rtc_info->date.week  = tm.tm_wday;

  return true;
}

bool rtcGetTime(rtc_time_t *rtc_time)
{
  rtc_info_t info;

  if (rtcGetInfo(&info) != true)
    return false;

  *rtc_time = info.time;
  return true;
}

bool rtcGetDate(rtc_date_t *rtc_date)
{
  rtc_info_t info;

  if (rtcGetInfo(&info) != true)
    return false;

  *rtc_date = info.date;
  return true;
}

static bool rtcSetTm(struct tm *p_tm)
{
  //-- newlib 의 mktime 은 TZ 가 없으면 UTC 로 계산한다
  //
  time_t t = mktime(p_tm);

  if (t < 0)
    return false;

  rtcSetSeconds((uint32_t)t);
  return true;
}

bool rtcSetTime(rtc_time_t *rtc_time)
{
  time_t    t = (time_t)rtcGetSeconds();
  struct tm tm;

  gmtime_r(&t, &tm);
  tm.tm_hour = rtc_time->hours;
  tm.tm_min  = rtc_time->minutes;
  tm.tm_sec  = rtc_time->seconds;

  return rtcSetTm(&tm);
}

bool rtcSetDate(rtc_date_t *rtc_date)
{
  time_t    t = (time_t)rtcGetSeconds();
  struct tm tm;

  gmtime_r(&t, &tm);
  tm.tm_year = (2000 + rtc_date->year) - 1900;
  tm.tm_mon  = rtc_date->month - 1;
  tm.tm_mday = rtc_date->day;

  return rtcSetTm(&tm);
}

bool rtcSetReg(uint32_t index, uint32_t data)
{
  if (index >= BBNSM_GPR_ARRAY_COUNT)
    return false;

  BBNSM->GPR[index] = data;
  return true;
}

bool rtcGetReg(uint32_t index, uint32_t *p_data)
{
  if (index >= BBNSM_GPR_ARRAY_COUNT)
    return false;

  *p_data = BBNSM->GPR[index];
  return true;
}


#if CLI_USE(HW_RTC)
void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    rtc_info_t rtc_info;

    cliPrintf("is_init  : %d\n", is_init);
    cliPrintf("ctrl     : 0x%08X\n", (unsigned)BBNSM->BBNSM_CTRL);
    cliPrintf("counter  : 0x%04X_%08X (32.768 kHz)\n",
              (unsigned)BBNSM->BBNSM_RTC_MS, (unsigned)BBNSM->BBNSM_RTC_LS);
    cliPrintf("seconds  : %u%s\n", (unsigned)rtcGetSeconds(), rtcIsTimeSet() ? "" : " (not set)");
    {
      //-- rtc_date_t 의 연도는 두 자리(20xx)다. 시각을 맞추기 전에는 1970 년이라
      //   여기서는 4 자리로 직접 찍는다.
      //
      time_t    t = (time_t)rtcGetSeconds();
      struct tm tm;

      (void)rtc_info;
      gmtime_r(&t, &tm);
      cliPrintf("Date     : %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
    for (int i=0; i<BBNSM_GPR_ARRAY_COUNT; i++)
    {
      cliPrintf("GPR%d     : 0x%08X\n", i, (unsigned)BBNSM->GPR[i]);
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "get") && args->isStr(1, "info"))
  {
    rtc_info_t rtc_info;

    while(cliKeepLoop())
    {
      rtcGetInfo(&rtc_info);

      cliPrintf("Y:%02d M:%02d D:%02d, H:%02d M:%02d S:%02d\n",
                rtc_info.date.year,
                rtc_info.date.month,
                rtc_info.date.day,
                rtc_info.time.hours,
                rtc_info.time.minutes,
                rtc_info.time.seconds);
      delay(1000);
    }
    ret = true;
  }

  if (args->argc == 5 && args->isStr(0, "set") && args->isStr(1, "time"))
  {
    rtc_time_t rtc_time;

    rtc_time.hours   = args->getData(2);
    rtc_time.minutes = args->getData(3);
    rtc_time.seconds = args->getData(4);

    rtcSetTime(&rtc_time);
    cliPrintf("H:%02d M:%02d S:%02d\n",
              rtc_time.hours,
              rtc_time.minutes,
              rtc_time.seconds);
    ret = true;
  }

  if (args->argc == 5 && args->isStr(0, "set") && args->isStr(1, "date"))
  {
    rtc_date_t rtc_date;

    rtc_date.year  = args->getData(2);
    rtc_date.month = args->getData(3);
    rtc_date.day   = args->getData(4);

    rtcSetDate(&rtc_date);
    cliPrintf("Y:%02d M:%02d D:%02d\n",
              rtc_date.year,
              rtc_date.month,
              rtc_date.day);
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "reg"))
  {
    uint32_t index = args->getData(1);
    uint32_t data  = args->getData(2);

    if (rtcSetReg(index, data))
    {
      rtcGetReg(index, &data);
      cliPrintf("GPR%d : 0x%08X\n", (int)index, (unsigned int)data);
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "reg"))
  {
    uint32_t index = args->getData(1);
    uint32_t data  = 0;

    if (rtcGetReg(index, &data))
    {
      cliPrintf("GPR%d : 0x%08X\n", (int)index, (unsigned int)data);
    }
    ret = true;
  }


  if (ret == false)
  {
    cliPrintf("rtc info\n");
    cliPrintf("rtc get info\n");
    cliPrintf("rtc set time [h] [m] [s]\n");
    cliPrintf("rtc set date [y] [m] [d]\n");
    cliPrintf("rtc reg [index] [data]\n");
  }
}
#endif

#endif
