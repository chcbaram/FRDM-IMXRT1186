#include "module.h"


/*
 * CLI 모듈 — titan-mini 의 ap/modules/common/cli 와 같다 (docs/28).
 *
 * CLI 를 main 루프에서 떼어 자기 스레드에서 돌린다. 명령이 오래 걸려도(qspi test,
 * psram test, peer echo …) main 스레드의 모듈 update 와 LED 는 계속 돈다.
 */

static bool cliModuleInit(void);
static void cliThread(void *arg);

static uint8_t  cli_ch   = HW_UART_CH_CLI;
static uint32_t cli_baud = 115200;


MODULE_DEF(cli)
{
  .name     = "cli",
  .priority = MODULE_PRI_NORMAL,
  .init     = cliModuleInit
};




bool cliModuleInit(void)
{
  cliOpen(cli_ch, cli_baud);

  return (threadCreate("cli", cliThread, NULL,
                       _HW_DEF_THREAD_CLI_PRI, _HW_DEF_THREAD_CLI_STACK) >= 0);
}

void cliThread(void *arg)
{
  (void)arg;

  logPrintf("[  ] Thread Started : cli\r\n");

  while (1)
  {
    cliMain();

    //-- 다른 채널(USB CDC 등)이 붙으면 여기서 고른다. 지금은 UART 하나뿐이다.

    delay(2);
  }
}
