#include "log.h"


#ifdef _USE_HW_LOG
#include "uart.h"
#include "cli.h"

#ifdef _USE_HW_RTOS
#define lock()      xSemaphoreTake(mutex_lock, portMAX_DELAY);
#define unLock()    xSemaphoreGive(mutex_lock);
#else
#define lock()      
#define unLock()    
#endif


typedef struct
{
  uint16_t line_index;
  uint16_t buf_length;
  uint16_t buf_length_max;
  uint16_t buf_index;
  uint8_t *buf;
} log_buf_t;


log_buf_t log_buf_boot;
log_buf_t log_buf_list;

static uint8_t buf_boot[LOG_BOOT_BUF_MAX];
static uint8_t buf_list[LOG_LIST_BUF_MAX];

static bool is_init = false;
static bool is_boot_log = true;
static bool is_enable = true;
static bool is_open = false;

static uint8_t  log_ch = LOG_CH;
static uint32_t log_baud = 115200;

static char print_buf[256];

#ifdef _USE_HW_RTOS
static SemaphoreHandle_t mutex_lock;
#endif



#if CLI_USE(HW_LOG)
static void cliCmd(cli_args_t *args);
#endif





bool logInit(void)
{
#ifdef _USE_HW_RTOS
  mutex_lock = xSemaphoreCreateMutex();
#endif
  
  log_buf_boot.line_index     = 0;
  log_buf_boot.buf_length     = 0;
  log_buf_boot.buf_length_max = LOG_BOOT_BUF_MAX;
  log_buf_boot.buf_index      = 0;
  log_buf_boot.buf            = buf_boot;


  log_buf_list.line_index     = 0;
  log_buf_list.buf_length     = 0;
  log_buf_list.buf_length_max = LOG_LIST_BUF_MAX;
  log_buf_list.buf_index      = 0;
  log_buf_list.buf            = buf_list;


  is_init = true;

#if CLI_USE(HW_LOG)
  cliAdd("log", cliCmd);
#endif

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

void logBoot(uint8_t enable)
{
  is_boot_log = enable;
}

bool logOpen(uint8_t ch, uint32_t baud)
{
  log_ch   = ch;
  log_baud = baud;
  is_open  = true;

  is_open = uartOpen(ch, baud);

  return is_open;
}

bool logIsOpen(void)
{
  return is_open;
}

bool logBufPrintf(log_buf_t *p_log, char *p_data, uint32_t length)
{
  uint32_t buf_last;
  uint8_t *p_buf;
  int buf_len;


  buf_last = p_log->buf_index + length + 8;
  if (buf_last > p_log->buf_length_max)
  {
    p_log->buf_index = 0;
    buf_last = p_log->buf_index + length + 8;

    if (buf_last > p_log->buf_length_max)
    {
      return false;
    }
  }

  p_buf = &p_log->buf[p_log->buf_index];

  buf_len = snprintf((char *)p_buf, length + 8, "%04X\t%s", p_log->line_index, p_data);
  p_log->line_index++;
  p_log->buf_index += buf_len;


  if (buf_len + p_log->buf_length <= p_log->buf_length_max)
  {
    p_log->buf_length += buf_len;
  }

  return true;
}

void logPrintf(const char *fmt, ...)
{
  lock();

  va_list args;
  int len;

  if (is_init != true) return;


  va_start(args, fmt);
  len = vsnprintf(print_buf, 256, fmt, args);

  if (is_open == true && is_enable == true)
  {
    uartWrite(log_ch, (uint8_t *)print_buf, len);
  }

  if (is_boot_log)
  {
    logBufPrintf(&log_buf_boot, print_buf, len);
  }
  logBufPrintf(&log_buf_list, print_buf, len);

  va_end(args);

  unLock();
}


#if CLI_USE(HW_LOG)
void cliCmd(cli_args_t *args)
{
  bool ret = false;



  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("boot.line_index %d\n", log_buf_boot.line_index);
    cliPrintf("boot.buf_length %d\n", log_buf_boot.buf_length);
    cliPrintf("\n");
    cliPrintf("list.line_index %d\n", log_buf_list.line_index);
    cliPrintf("list.buf_length %d\n", log_buf_list.buf_length);

    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "boot"))
  {
    uint32_t index = 0;
    uint32_t index_begin;

    //-- 가장 오래된 것부터 읽는다. 최신이 마지막에 나와야 한다.
    //   buf_index 는 다음에 쓸 자리이고, 한 바퀴 돌았으면 그 자리가 곧 가장
    //   오래된 데이터다. 아직 안 돌았으면 이 식이 0 이 된다.
    //
    index_begin = (log_buf_boot.buf_length_max + log_buf_boot.buf_index) - log_buf_boot.buf_length;

    while(cliKeepLoop())
    {
      uint32_t buf_len;
      uint32_t buf_pos;

      buf_len = log_buf_boot.buf_length - index;
      if (buf_len == 0)
      {
        break;
      }
      if (buf_len > 64)
      {
        buf_len = 64;
      }

      lock();

      buf_pos = (index_begin + index) % log_buf_boot.buf_length_max;
      if ((buf_pos + buf_len) > log_buf_boot.buf_length_max)
      {
        buf_len = log_buf_boot.buf_length_max - buf_pos;
      }

      cliWrite((uint8_t *)&log_buf_boot.buf[buf_pos], buf_len);
      index += buf_len;

      unLock();
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "list"))
  {
    uint32_t index = 0;
    uint32_t index_begin;

    //-- boot 와 같다. 링버퍼를 가장 오래된 자리부터 감아 읽는다.
    //
    index_begin = (log_buf_list.buf_length_max + log_buf_list.buf_index) - log_buf_list.buf_length;

    while(cliKeepLoop())
    {
      uint32_t buf_len;
      uint32_t buf_pos;

      buf_len = log_buf_list.buf_length - index;
      if (buf_len == 0)
      {
        break;
      }
      if (buf_len > 64)
      {
        buf_len = 64;
      }

      lock();

      buf_pos = (index_begin + index) % log_buf_list.buf_length_max;
      if ((buf_pos + buf_len) > log_buf_list.buf_length_max)
      {
        buf_len = log_buf_list.buf_length_max - buf_pos;
      }

      cliWrite((uint8_t *)&log_buf_list.buf[buf_pos], buf_len);
      index += buf_len;

      unLock();
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("log info\n");
    cliPrintf("log boot\n");
    cliPrintf("log list\n");
  }
}
#endif


#endif