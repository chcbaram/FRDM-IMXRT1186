#include "log.h"


#ifdef _USE_HW_LOG
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*
 * 로그 — CM7 쪽 (docs/28-freertos.md)
 *
 * CM7 에는 UART 가 없다. logPrintf() 는 DTCM 버퍼에 차례로 쌓기만 한다. 가득 차면 더 쓰지
 * 않는다(부팅 로그를 남기는 용도). CM33 의 CLI `peer log` 가 cmd 패킷(SHARED_CMD_PEER_LOG)
 * 으로 logReadBoot() 를 불러 가져가 찍는다. API 는 CM33 log.c 와 같다.
 */

#ifdef _USE_HW_RTOS
#define lock()      if (rtosIsRunning()) xSemaphoreTake(mutex_lock, portMAX_DELAY);
#define unLock()    if (rtosIsRunning()) xSemaphoreGive(mutex_lock);
#else
#define lock()
#define unLock()
#endif


static bool     is_init   = false;
static bool     is_enable = true;
static char     buf_boot[LOG_BOOT_BUF_MAX];
static uint32_t buf_len   = 0;
#ifdef _USE_HW_RTOS
static SemaphoreHandle_t mutex_lock;
#endif




bool logInit(void)
{
#ifdef _USE_HW_RTOS
  mutex_lock = xSemaphoreCreateMutex();
#endif
  buf_len = 0;
  is_init = true;
  return true;
}

void logEnable(void)
{
  is_enable = true;
}

void logDisable(void)
{
  is_enable = false;
}

bool logOpen(uint8_t ch, uint32_t baud)
{
  (void)ch;
  (void)baud;
  return false;                     // 내보낼 UART 가 없다
}

bool logIsOpen(void)
{
  return false;
}

void logBoot(uint8_t enable)
{
  is_enable = enable;
}

void logPrintf(const char *fmt, ...)
{
  va_list args;
  int     len;
  char    print_buf[128];

  if (is_init != true || is_enable != true)
    return;

  va_start(args, fmt);
  len = vsnprintf(print_buf, sizeof(print_buf), fmt, args);
  va_end(args);

  if (len <= 0)
    return;
  if (len > (int)sizeof(print_buf) - 1)
    len = sizeof(print_buf) - 1;

  lock();
  if (buf_len + len <= LOG_BOOT_BUF_MAX)
  {
    memcpy(&buf_boot[buf_len], print_buf, len);
    buf_len += len;
  }
  unLock();
}

uint32_t logReadBoot(uint32_t offset, uint8_t *p_data, uint32_t length)
{
  uint32_t n = 0;

  lock();
  if (offset < buf_len)
  {
    n = buf_len - offset;
    if (n > length)
      n = length;
    memcpy(p_data, &buf_boot[offset], n);
  }
  unLock();
  return n;
}

#endif
