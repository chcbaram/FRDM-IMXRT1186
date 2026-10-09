#include "uart.h"
#include "qbuffer.h"
#include "cli.h"

#ifdef _USE_HW_UART
#include "fsl_lpuart.h"
#include "fsl_iomuxc.h"
#include "fsl_clock.h"

/*
 * LPUART1 — MCU-Link VCOM (docs/03-board-mapping.md 5절)
 *
 *   TX  GPIO_AON_08  ALT0   J43 1-2 (기본)
 *   RX  GPIO_AON_09  ALT0   J41 1-2 (기본)
 *
 * 레벨 변환기 U30 을 지나 MCU-Link 의 USB-UART 로 간다. 케이블 하나(J23)로
 * 기록, 디버그, 콘솔이 다 된다. BootROM 시리얼 다운로더도 같은 UART 를 쓴다.
 *
 * 클럭: LPUART1/2 는 클럭 루트 하나(LPUART0102)를 공유한다.
 *   SysPll3Div2 (240 MHz) / 10 = 24 MHz
 * PLL3 은 ROM 이 FlexSPI 클럭(PLL3_PFD2) 때문에 켜 둔 상태를 그대로 쓴다.
 * 클럭을 직접 잡는 것은 로드맵 22 에서 한다.
 *
 * 수신은 인터럽트로 qbuffer 에 넣고, 송신은 폴링이다.
 */

#define UART_RX_BUF_LENGTH        1024


typedef struct
{
  bool     is_open;
  uint32_t baud;

  uint8_t   rx_buf[UART_RX_BUF_LENGTH];
  qbuffer_t qbuffer;

  uint32_t rx_cnt;
  uint32_t tx_cnt;
} uart_tbl_t;

typedef struct
{
  const char      *p_msg;
  LPUART_Type     *p_base;
  IRQn_Type        irq;
  clock_root_t     clk_root;
  clock_root_config_t clk_cfg;
  uint32_t         tx_mux[5];
  uint32_t         rx_mux[5];
  uart_driver_t   *p_driver;
} uart_hw_t;


#if CLI_USE(HW_UART)
static void cliUart(cli_args_t *args);
#endif


static bool is_init = false;

static uart_tbl_t uart_tbl[UART_MAX_CH];

static uart_hw_t uart_hw_tbl[UART_MAX_CH] =
  {
    {"LPUART1 MCU-Link", LPUART1, LPUART1_IRQn,
     kCLOCK_Root_Lpuart0102, {.clockOff = false, .mux = kCLOCK_LPUART0102_ClockRoot_MuxSysPll3Div2, .div = 10},
     {IOMUXC_GPIO_AON_08_LPUART1_TX}, {IOMUXC_GPIO_AON_09_LPUART1_RX},
     NULL},
  };




bool uartInit(void)
{
  for (int i = 0; i < UART_MAX_CH; i++)
  {
    uart_tbl[i].is_open = false;
    uart_tbl[i].baud    = 115200;
    uart_tbl[i].rx_cnt  = 0;
    uart_tbl[i].tx_cnt  = 0;
  }

  is_init = true;

#if CLI_USE(HW_UART)
  cliAdd("uart", cliUart);
#endif

  return true;
}

bool uartDeInit(void)
{
  return true;
}

bool uartIsInit(void)
{
  return is_init;
}

bool uartSetDriver(uint8_t ch, uart_driver_t *p_driver)
{
  if (ch >= UART_MAX_CH) return false;

  uart_hw_tbl[ch].p_driver = p_driver;
  return true;
}

bool uartOpen(uint8_t ch, uint32_t baud)
{
  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].is_open == true && uart_tbl[ch].baud == baud)
  {
    return true;
  }

  if (uart_hw_tbl[ch].p_driver != NULL)
  {
    bool ret = uart_hw_tbl[ch].p_driver->open(baud);
    uart_tbl[ch].is_open = ret;
    uart_tbl[ch].baud    = baud;
    return ret;
  }

  uart_hw_t         *p_hw = &uart_hw_tbl[ch];
  lpuart_config_t    config;

  qbufferCreate(&uart_tbl[ch].qbuffer, uart_tbl[ch].rx_buf, UART_RX_BUF_LENGTH);

  if (uart_tbl[ch].is_open == true)
  {
    NVIC_DisableIRQ(p_hw->irq);
    LPUART_Deinit(p_hw->p_base);
    uart_tbl[ch].is_open = false;
  }

  //-- 핀 : AON 도메인 IOMUXC 는 LPCG Iomuxc2 다 (LED 의 EMC 핀은 Iomuxc1)
  //
  CLOCK_EnableClock(kCLOCK_Iomuxc2);
  IOMUXC_SetPinMux(p_hw->tx_mux[0], p_hw->tx_mux[1], p_hw->tx_mux[2], p_hw->tx_mux[3], p_hw->tx_mux[4], 0U);
  IOMUXC_SetPinMux(p_hw->rx_mux[0], p_hw->rx_mux[1], p_hw->rx_mux[2], p_hw->rx_mux[3], p_hw->rx_mux[4], 0U);
  IOMUXC_SetPinConfig(p_hw->tx_mux[0], p_hw->tx_mux[1], p_hw->tx_mux[2], p_hw->tx_mux[3], p_hw->tx_mux[4], 0x02U);
  IOMUXC_SetPinConfig(p_hw->rx_mux[0], p_hw->rx_mux[1], p_hw->rx_mux[2], p_hw->rx_mux[3], p_hw->rx_mux[4], 0x02U);

  //-- 클럭 루트
  //
  CLOCK_SetRootClock(p_hw->clk_root, &p_hw->clk_cfg);

  LPUART_GetDefaultConfig(&config);
  config.baudRate_Bps = baud;
  config.enableTx     = true;
  config.enableRx     = true;

  if (LPUART_Init(p_hw->p_base, &config, CLOCK_GetRootClockFreq(p_hw->clk_root)) != kStatus_Success)
  {
    return false;
  }

  LPUART_EnableInterrupts(p_hw->p_base, kLPUART_RxDataRegFullInterruptEnable | kLPUART_RxOverrunInterruptEnable);
  NVIC_SetPriority(p_hw->irq, 5);
  NVIC_EnableIRQ(p_hw->irq);

  uart_tbl[ch].is_open = true;
  uart_tbl[ch].baud    = baud;

  return true;
}

bool uartIsOpen(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  return uart_tbl[ch].is_open;
}

bool uartClose(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  if (uart_hw_tbl[ch].p_driver != NULL)
  {
    uart_tbl[ch].is_open = false;
    return uart_hw_tbl[ch].p_driver->close();
  }

  if (uart_tbl[ch].is_open == true)
  {
    NVIC_DisableIRQ(uart_hw_tbl[ch].irq);
    LPUART_Deinit(uart_hw_tbl[ch].p_base);
    uart_tbl[ch].is_open = false;
  }

  return true;
}

uint32_t uartAvailable(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  if (uart_hw_tbl[ch].p_driver != NULL)
  {
    return uart_hw_tbl[ch].p_driver->available();
  }

  return qbufferAvailable(&uart_tbl[ch].qbuffer);
}

bool uartFlush(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  if (uart_hw_tbl[ch].p_driver != NULL)
  {
    return uart_hw_tbl[ch].p_driver->flush();
  }

  qbufferFlush(&uart_tbl[ch].qbuffer);
  return true;
}

uint8_t uartRead(uint8_t ch)
{
  uint8_t ret = 0;

  if (ch >= UART_MAX_CH) return 0;

  if (uart_hw_tbl[ch].p_driver != NULL)
  {
    return uart_hw_tbl[ch].p_driver->read();
  }

  qbufferRead(&uart_tbl[ch].qbuffer, &ret, 1);
  return ret;
}

uint32_t uartWrite(uint8_t ch, uint8_t *p_data, uint32_t length)
{
  if (ch >= UART_MAX_CH) return 0;
  if (length == 0) return 0;

  if (uart_hw_tbl[ch].p_driver != NULL)
  {
    return uart_hw_tbl[ch].p_driver->write(p_data, length);
  }

  if (uart_tbl[ch].is_open != true) return 0;

  //-- 폴링 송신. 115200 bps 에서 256 B 가 약 22 ms 다.
  //   로그가 많아지면 DMA 로 바꾼다.
  //
  if (LPUART_WriteBlocking(uart_hw_tbl[ch].p_base, p_data, length) != kStatus_Success)
  {
    return 0;
  }

  uart_tbl[ch].tx_cnt += length;
  return length;
}

uint32_t uartPrintf(uint8_t ch, const char *fmt, ...)
{
  char buf[256];
  va_list args;
  int len;

  va_start(args, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (len <= 0) return 0;
  if (len > (int)sizeof(buf)) len = (int)sizeof(buf);

  return uartWrite(ch, (uint8_t *)buf, (uint32_t)len);
}

uint32_t uartGetBaud(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  return uart_tbl[ch].baud;
}

uint32_t uartGetRxCnt(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  return uart_tbl[ch].rx_cnt;
}

uint32_t uartGetTxCnt(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  return uart_tbl[ch].tx_cnt;
}

static void uartRxHandler(uint8_t ch)
{
  LPUART_Type *p_base = uart_hw_tbl[ch].p_base;
  uint32_t     status = LPUART_GetStatusFlags(p_base);

  //-- 오버런이 나면 수신이 멈춘다. 플래그를 지워야 다시 받는다.
  //
  if (status & kLPUART_RxOverrunFlag)
  {
    LPUART_ClearStatusFlags(p_base, kLPUART_RxOverrunFlag);
  }

  while (LPUART_GetStatusFlags(p_base) & kLPUART_RxDataRegFullFlag)
  {
    uint8_t rx_data = LPUART_ReadByte(p_base);

    qbufferWrite(&uart_tbl[ch].qbuffer, &rx_data, 1);
    uart_tbl[ch].rx_cnt++;
  }
}

void LPUART1_IRQHandler(void)
{
  uartRxHandler(_DEF_UART1);
  __DSB();
}

//-- newlib 의 printf 출력을 콘솔로 보낸다 (bsp/syscalls.c 의 weak _write 를 덮는다)
//
int _write(int file, char *ptr, int len)
{
  (void)file;

  uartWrite(HW_UART_CH_CLI, (uint8_t *)ptr, (uint32_t)len);
  return len;
}


#if CLI_USE(HW_UART)
void cliUart(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    for (int i = 0; i < UART_MAX_CH; i++)
    {
      cliPrintf("_DEF_UART%d : %s %d bps, clk %d Hz",
                i + 1, uart_hw_tbl[i].p_msg, (int)uartGetBaud(i),
                (int)CLOCK_GetRootClockFreq(uart_hw_tbl[i].clk_root));
      cliPrintf("  rx %d, tx %d\n", (int)uartGetRxCnt(i), (int)uartGetTxCnt(i));
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "test"))
  {
    uint8_t ch = (uint8_t)(args->getData(1) - 1);

    if (ch >= UART_MAX_CH)
    {
      cliPrintf("ch %d 는 범위 밖이다\n", ch + 1);
      return;
    }

    while (cliKeepLoop())
    {
      if (uartAvailable(ch) > 0)
      {
        uartPrintf(ch, "%c", uartRead(ch));
      }
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("uart info\n");
    cliPrintf("uart test ch[1~%d]\n", HW_UART_MAX_CH);
  }
}
#endif

#endif
