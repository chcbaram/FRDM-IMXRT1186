#include "ipc.h"

#ifdef _USE_HW_IPC
#include "cli.h"
#include "shared.h"
#include "cm7.h"


/*
 * 주 코어(CM33) 쪽 — titan-mini 의 cm85 ipc.c 와 같은 설계에 이미지 검사를 더했다.
 *
 *   1. QSPI CM7 슬롯의 헤더 검사 (magic, 크기, CRC-32)
 *   2. 공유 블록 magic 을 0 으로 지운다 (낡은 값 방지, ipc.h 주석)
 *   3. cm7Prepare → cm7Load → cm7Start (bsp/cm7.c)
 *   4. CM7 이 magic 을 쓸 때까지 기다린다
 *
 * 공유 블록은 두 코어가 각자 정의하고 링커가 .shared 를 같은 주소(OCRAM2)에 놓는다.
 */
__attribute__((section(".shared"), used))
static shared_t shared;


#if CLI_USE(HW_IPC)
static void cliIpc(cli_args_t *args);
#endif

#if _HW_DEF_CM7_IMAGE
static bool     ipcCheckImage(void);
static uint32_t crc32(const uint8_t *p_data, uint32_t length);
#endif


static IpcState_t ipc_state    = IPC_STATE_DISABLED;
static uint32_t   boot_time_ms = 0;
static cm7_err_t  start_err    = CM7_OK;
static uint32_t   image_crc    = 0;
#if _HW_DEF_CM7_IMAGE
static uint32_t   image_size   = 0;
#endif

static uint32_t   last_alive    = 0;
static uint32_t   last_alive_ms = 0;

static const char *ipc_state_str[] =
  {
    "DISABLED",
    "NO_IMAGE",
    "START_FAIL",
    "TIMEOUT",
    "BAD_VERSION",
    "RUNNING",
  };


bool ipcInit(void)
{
#if _HW_DEF_CM7_IMAGE
  uint32_t pre_ms;

  if (ipcCheckImage() != true)
  {
    ipc_state = IPC_STATE_NO_IMAGE;
  }
  else
  {
    shared.magic = 0;
    __DMB();

    pre_ms = millis();

    cm7Prepare();
    cm7Load((const cm7_image_t *)SHARED_CM7_IMAGE_ADDR + 1, image_size);
    start_err = cm7Start();

    if (start_err != CM7_OK)
    {
      ipc_state = IPC_STATE_START_FAIL;
    }
    else
    {
      ipc_state = IPC_STATE_TIMEOUT;

      while ((millis() - pre_ms) < HW_IPC_BOOT_TIMEOUT_MS)
      {
        if (shared.magic == SHARED_MAGIC)
        {
          __DMB();
          ipc_state = (shared.version == SHARED_VERSION) ? IPC_STATE_RUNNING : IPC_STATE_BAD_VERSION;
          break;
        }
      }
    }

    boot_time_ms  = millis() - pre_ms;
    last_alive    = shared.peer_alive;
    last_alive_ms = millis();
  }
#else
  //-- CM7 이미지를 함께 빌드하지 않았다 (BUILD_CM7=OFF). 깨우지 않는다.
  //
  ipc_state = IPC_STATE_DISABLED;
#endif

#if CLI_USE(HW_IPC)
  cliAdd("ipc", cliIpc);
#endif

  return (ipc_state == IPC_STATE_RUNNING);
}

IpcState_t ipcGetState(void)
{
  return ipc_state;
}

const char *ipcGetStateStr(void)
{
  return ipc_state_str[ipc_state];
}

bool ipcIsBooted(void)
{
  return (ipc_state == IPC_STATE_RUNNING);
}

bool ipcIsRunning(void)
{
  uint32_t alive;

  if (ipc_state != IPC_STATE_RUNNING)
    return false;

  if (shared.magic != SHARED_MAGIC)
    return false;

  alive = shared.peer_alive;
  if (alive != last_alive)
  {
    last_alive    = alive;
    last_alive_ms = millis();
  }

  return ((millis() - last_alive_ms) < HW_IPC_ALIVE_TIMEOUT_MS);
}

uint32_t ipcGetBootTime(void)
{
  return boot_time_ms;
}

uint32_t ipcGetAliveCnt(void)
{
  return shared.peer_alive;
}

uint32_t ipcGetTick(void)
{
  return shared.peer_tick;
}

const char *ipcGetName(void)
{
  return ipcIsBooted() ? (const char *)shared.peer_name : "-";
}

const char *ipcGetVersion(void)
{
  return ipcIsBooted() ? (const char *)shared.peer_fw_ver : "-";
}

uint32_t ipcGetClock(void)
{
  return ipcIsBooted() ? shared.peer_clock : 0;
}


#if _HW_DEF_CM7_IMAGE
static uint32_t crc32(const uint8_t *p_data, uint32_t length)
{
  uint32_t crc = 0xFFFFFFFFUL;

  for (uint32_t i = 0; i < length; i++)
  {
    crc ^= p_data[i];
    for (int b = 0; b < 8; b++)
    {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

static bool ipcCheckImage(void)
{
  const cm7_image_t *p_hdr = (const cm7_image_t *)SHARED_CM7_IMAGE_ADDR;

  if (p_hdr->magic != SHARED_CM7_IMAGE_MAGIC)
    return false;
  if (p_hdr->size == 0 || p_hdr->size > SHARED_CM7_IMAGE_MAX)
    return false;

  image_size = p_hdr->size;
  image_crc  = crc32((const uint8_t *)(p_hdr + 1), p_hdr->size);

  return (image_crc == p_hdr->crc32);
}
#endif

#if CLI_USE(HW_IPC)
void cliIpc(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    const cm7_image_t *p_hdr = (const cm7_image_t *)SHARED_CM7_IMAGE_ADDR;

    cliPrintf("peer       : CM7\n");
    cliPrintf("build      : %s\n", _HW_DEF_CM7_IMAGE ? "BUILD_CM7=ON" : "BUILD_CM7=OFF");
    cliPrintf("state      : %s\n", ipcGetStateStr());
    cliPrintf("image      : 0x%08X magic 0x%08X size %d crc 0x%08X (calc 0x%08X)\n",
              (unsigned)SHARED_CM7_IMAGE_ADDR, (unsigned)p_hdr->magic, (int)p_hdr->size,
              (unsigned)p_hdr->crc32, (unsigned)image_crc);
    cliPrintf("ele resp   : 0x%08X 0x%08X (err %d)\n",
              (unsigned)cm7GetEleResp(0), (unsigned)cm7GetEleResp(1), (int)start_err);

    if (ipc_state == IPC_STATE_RUNNING || ipc_state == IPC_STATE_BAD_VERSION)
    {
      uint32_t a0 = shared.peer_alive;
      uint32_t t0 = shared.peer_tick;

      cliPrintf("name       : %s\n", (const char *)shared.peer_name);
      cliPrintf("fw ver     : %s\n", (const char *)shared.peer_fw_ver);
      cliPrintf("clock      : %d MHz\n", (int)(shared.peer_clock / 1000000));
      cliPrintf("boot time  : %d ms\n", (int)boot_time_ms);
      cliPrintf("magic      : 0x%08X\n", (unsigned)shared.magic);
      cliPrintf("version    : %d (expect %d)\n", (int)shared.version, (int)SHARED_VERSION);

      delay(500);

      cliPrintf("alive      : %d  (+%d / 500ms)\n",
                (int)shared.peer_alive, (int)(shared.peer_alive - a0));
      cliPrintf("tick       : %d ms  (+%d)\n",
                (int)shared.peer_tick, (int)(shared.peer_tick - t0));
      cliPrintf("running    : %s\n", ipcIsRunning() ? "yes" : "no");
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("ipc info\n");
  }
}
#endif

#endif
