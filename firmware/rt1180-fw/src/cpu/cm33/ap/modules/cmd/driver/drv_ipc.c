#include "driver/drv_ipc.h"

#ifdef _USE_HW_CMD


/* 상대 코어 cmd 채널. ipc 스트림(공유 링 버퍼 + MU doorbell) 위에 cmd 패킷을 올린다.
   N657X0 의 drv_uart.c 와 같은 자리다. 패킷 처리 코드는 채널 종류를 모른다. */
#define DRV_IPC_TX_TIMEOUT    10            // ms. 상대가 링을 비울 때까지 기다리는 시간
#define DRV_IPC_RX_CHUNK      64            // 링에서 한 번에 가져오는 크기. cmd 파서는 바이트씩 읽는다


static bool     drvIpcOpen(void *args);
static bool     drvIpcClose(void *args);
static uint32_t drvIpcAvailable(void *args);
static bool     drvIpcFlush(void *args);
static uint8_t  drvIpcRead(void *args);
static uint32_t drvIpcWrite(void *args, uint8_t *p_data, uint32_t length);


static uint8_t  rx_chunk[DRV_IPC_RX_CHUNK];
static uint32_t rx_index  = 0;
static uint32_t rx_length = 0;




bool drvIpcInit(cmd_driver_t *p_driver)
{
  p_driver->open      = drvIpcOpen;
  p_driver->close     = drvIpcClose;
  p_driver->available = drvIpcAvailable;
  p_driver->flush     = drvIpcFlush;
  p_driver->read      = drvIpcRead;
  p_driver->write     = drvIpcWrite;
  p_driver->set_baud  = NULL;
  p_driver->get_baud  = NULL;

  return true;
}

bool drvIpcOpen(void *args)
{
  (void)args;
  return true;
}

bool drvIpcClose(void *args)
{
  (void)args;
  return true;
}

uint32_t drvIpcAvailable(void *args)
{
  (void)args;

  //-- 비었을 때만 링에서 덩어리로 가져온다. 링의 head/tail 은 non-cacheable 이라
  //   바이트마다 묻지 않는다.
  //
  if (rx_index >= rx_length)
  {
    rx_index  = 0;
    rx_length = ipcStreamRead(rx_chunk, DRV_IPC_RX_CHUNK);
  }
  return rx_length - rx_index;
}

bool drvIpcFlush(void *args)
{
  (void)args;
  rx_index  = 0;
  rx_length = 0;
  ipcStreamFlush();
  return true;
}

uint8_t drvIpcRead(void *args)
{
  if (drvIpcAvailable(args) == 0)
    return 0;

  return rx_chunk[rx_index++];
}

uint32_t drvIpcWrite(void *args, uint8_t *p_data, uint32_t length)
{
  (void)args;
  return ipcStreamWrite(p_data, length, DRV_IPC_TX_TIMEOUT);
}

#endif
