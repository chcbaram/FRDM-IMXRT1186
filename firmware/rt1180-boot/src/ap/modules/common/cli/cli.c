#include "module.h"


/*
 * CLI 모듈 — 부트로더는 RTOS 가 없어서 스레드 대신 moduleUpdate() 에서 cliMain() 을 부른다.
 * 앱(rt1180-fw)의 같은 모듈은 자기 스레드에서 돈다 (docs/28).
 */

static bool cliModuleInit(void);
static void cliModuleUpdate(void const *arg);

static uint8_t  cli_ch   = HW_UART_CH_CLI;
static uint32_t cli_baud = 115200;


MODULE_DEF(cli)
{
  .name     = "cli",
  .priority = MODULE_PRI_NORMAL,
  .init     = cliModuleInit,
  .update   = cliModuleUpdate
};




bool cliModuleInit(void)
{
  return cliOpen(cli_ch, cli_baud);
}

void cliModuleUpdate(void const *arg)
{
  (void)arg;

  cliMain();
}
