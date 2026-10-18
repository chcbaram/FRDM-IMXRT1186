#include "ipc.h"

#ifdef _USE_HW_IPC
#include "cli.h"
#include "shared.h"
#include "cm7.h"
#include "qbuffer.h"
#include "fsl_mu.h"
#include <string.h>


/*
 * 주 코어(CM33) 쪽 — titan-mini 의 cm85 ipc.c 와 같은 설계에 이미지 검사를 더했다.
 *
 *   1. QSPI CM7 슬롯의 헤더 검사 (magic, 크기, CRC-32)
 *   2. 공유 블록 magic 을 0 으로 지운다 (낡은 값 방지, ipc.h 주석)
 *   3. cm7Prepare → cm7Load → cm7Start (bsp/cm7.c)
 *   4. CM7 이 magic 을 쓸 때까지 기다린다
 *
 * 메시지는 MU1 의 A 쪽(MU1_MUA)을 쓴다. CM7 은 B 쪽이다. 형식은 shared.h 3절.
 * MU 는 CM7 을 깨우기 전에 연다. CM7 이 기동하자마자 보내도 받을 수 있게.
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
static void     ipcMsgInit(void);
#endif
static void     ringCopyIn(uint8_t *p_ring, uint32_t pos, const uint8_t *p_src, uint32_t length);
static void     ringCopyOut(uint8_t *p_dst, const uint8_t *p_ring, uint32_t pos, uint32_t length);


static IpcState_t ipc_state    = IPC_STATE_DISABLED;
static uint32_t   boot_time_ms = 0;
static cm7_err_t  start_err    = CM7_OK;
static uint32_t   image_crc    = 0;
#if _HW_DEF_CM7_IMAGE
static uint32_t   image_size   = 0;
#endif

static uint32_t   last_alive    = 0;
static uint32_t   last_alive_ms = 0;

static bool       is_msg_init = false;
static qbuffer_t  msg_q;
static shared_ring_t * const p_tx = &shared.m2p;
static shared_ring_t * const p_rx = &shared.p2m;
static volatile uint32_t stream_event_cnt = 0;
static uint32_t   stream_tx_timeout = 0;
#if _HW_DEF_CM7_IMAGE
static ipc_msg_t  msg_q_buf[HW_IPC_MSG_Q_MAX];
#endif
static uint32_t   msg_tx_cnt     = 0;
static uint32_t   msg_rx_cnt     = 0;
static uint32_t   msg_tx_timeout = 0;
static uint32_t   msg_q_full     = 0;
static volatile uint32_t pong_cnt  = 0;
static volatile uint32_t pong_data = 0;

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

    ipcMsgInit();

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

bool ipcMsgSend(uint32_t id, uint32_t data, uint32_t timeout_ms)
{
  const uint32_t tx_empty = kMU_Tx0EmptyFlag | kMU_Tx1EmptyFlag;
  uint32_t       pre_ms;
  uint32_t       primask;

  if (is_msg_init != true || id == 0)
    return false;

  //-- 상대가 앞 메시지를 읽어 TR0/TR1 이 빌 때까지 기다린다. MU_SendMsg() 는
  //   끝없이 기다리므로 쓰지 않는다 (CM7 이 디버거로 멈춰 있으면 같이 멈춘다).
  //
  pre_ms = millis();
  while ((MU_GetStatusFlags(MU1_MUA) & tx_empty) != tx_empty)
  {
    if ((millis() - pre_ms) >= timeout_ms)
    {
      msg_tx_timeout++;
      return false;
    }
  }

  //-- data 다음 id. 두 쓰기 사이에 다른 송신이 끼지 않게 막는다.
  //
  primask = __get_PRIMASK();
  __disable_irq();
  MU_SendMsgNonBlocking(MU1_MUA, 1, data);
  MU_SendMsgNonBlocking(MU1_MUA, 0, id);
  __set_PRIMASK(primask);

  msg_tx_cnt++;
  return true;
}

uint32_t ipcMsgAvailable(void)
{
  return is_msg_init ? qbufferAvailable(&msg_q) : 0;
}

bool ipcMsgRead(ipc_msg_t *p_msg)
{
  if (ipcMsgAvailable() == 0)
    return false;

  return qbufferRead(&msg_q, (uint8_t *)p_msg, 1);
}

bool ipcPing(uint32_t timeout_ms, uint32_t *p_rtt_us)
{
  uint32_t seq = pong_cnt + 1;
  uint32_t pre_us;
  uint32_t pre_ms;

  pre_us = micros();
  if (ipcMsgSend(SHARED_MSG_PING, seq, timeout_ms) != true)
    return false;

  pre_ms = millis();
  while (pong_cnt == seq - 1)
  {
    if ((millis() - pre_ms) >= timeout_ms)
      return false;
  }
  *p_rtt_us = micros() - pre_us;

  return (pong_data == seq);
}

uint32_t ipcStreamAvailable(void)
{
  if (is_msg_init != true)
    return 0;

  return (p_rx->head - p_rx->tail) & (SHARED_RING_SIZE - 1);
}

uint32_t ipcStreamRead(uint8_t *p_data, uint32_t length)
{
  uint32_t avail = ipcStreamAvailable();
  uint32_t tail;

  if (length > avail)
    length = avail;
  if (length == 0)
    return 0;

  //-- head 를 읽은 뒤 데이터를 읽는다. 다 읽은 뒤에 tail 을 넘겨 자리를 돌려준다.
  //
  __DMB();
  tail = p_rx->tail;
  ringCopyOut(p_data, p_rx->buf, tail, length);
  __DMB();
  p_rx->tail = (tail + length) & (SHARED_RING_SIZE - 1);

  return length;
}

uint32_t ipcStreamWrite(uint8_t *p_data, uint32_t length, uint32_t timeout_ms)
{
  uint32_t written = 0;
  uint32_t pre_ms  = millis();

  if (is_msg_init != true)
    return 0;

  while (written < length)
  {
    uint32_t head  = p_tx->head;
    uint32_t space = (p_tx->tail - head - 1) & (SHARED_RING_SIZE - 1);
    uint32_t n     = length - written;

    if (space == 0)
    {
      if ((millis() - pre_ms) >= timeout_ms)
      {
        stream_tx_timeout++;
        break;
      }
      continue;
    }
    if (n > space)
      n = space;

    ringCopyIn(p_tx->buf, head, &p_data[written], n);
    __DMB();
    p_tx->head = (head + n) & (SHARED_RING_SIZE - 1);
    __DSB();

    //-- doorbell. 상대가 아직 앞 것을 지우지 않았으면 실패하지만 하나로 합쳐지므로 괜찮다.
    //
    (void)MU_TriggerInterrupts(MU1_MUA, kMU_GenInt0InterruptTrigger);
    written += n;
    pre_ms   = millis();
  }

  return written;
}

void ipcStreamFlush(void)
{
  if (is_msg_init != true)
    return;

  p_rx->tail = p_rx->head;
}

uint32_t ipcStreamGetEventCnt(void)
{
  return stream_event_cnt;
}

//-- 링 복사. 끝에서 넘어가면 두 번에 나눈다. 공유 블록은 Normal non-cacheable 이라 memcpy 의
//   워드 접근이 그대로 된다 (바이트마다 OCRAM 에 가는 것보다 빠르다).
//
static void ringCopyIn(uint8_t *p_ring, uint32_t pos, const uint8_t *p_src, uint32_t length)
{
  uint32_t first = SHARED_RING_SIZE - pos;

  if (first > length)
    first = length;
  memcpy(&p_ring[pos], p_src, first);
  memcpy(&p_ring[0], &p_src[first], length - first);
}

static void ringCopyOut(uint8_t *p_dst, const uint8_t *p_ring, uint32_t pos, uint32_t length)
{
  uint32_t first = SHARED_RING_SIZE - pos;

  if (first > length)
    first = length;
  memcpy(p_dst, &p_ring[pos], first);
  memcpy(&p_dst[first], &p_ring[0], length - first);
}

void MU1_IRQHandler(void)
{
  ipc_msg_t msg;

  if ((MU_GetStatusFlags(MU1_MUA) & kMU_GenInt0Flag) != 0)
  {
    MU_ClearStatusFlags(MU1_MUA, kMU_GenInt0Flag);
    stream_event_cnt++;
  }

  while ((MU_GetStatusFlags(MU1_MUA) & kMU_Rx0FullFlag) != 0)
  {
    msg.data = MU_ReceiveMsgNonBlocking(MU1_MUA, 1);
    msg.id   = MU_ReceiveMsgNonBlocking(MU1_MUA, 0);
    msg_rx_cnt++;

    if (msg.id == SHARED_MSG_PONG)
    {
      pong_data = msg.data;
      pong_cnt++;
    }
    else if (qbufferWrite(&msg_q, (uint8_t *)&msg, 1) != true)
    {
      msg_q_full++;
    }
  }
  __DSB();
}

#if _HW_DEF_CM7_IMAGE
static void ipcMsgInit(void)
{
  qbufferCreateBySize(&msg_q, (uint8_t *)msg_q_buf, sizeof(ipc_msg_t), HW_IPC_MSG_Q_MAX);

  //-- 두 링을 비운다. 상대 코어를 깨우기 전이라 아무도 쓰고 있지 않다.
  //
  shared.m2p.head = 0;
  shared.m2p.tail = 0;
  shared.p2m.head = 0;
  shared.p2m.tail = 0;

  //-- LPCG 를 켠다. MU 는 CM33 리셋만으로는 비워지지 않을 수 있어 남은 수신을 버린다.
  //
  MU_Init(MU1_MUA);
  for (uint32_t i = 0; i < MU_RR_COUNT; i++)
  {
    (void)MU_ReceiveMsgNonBlocking(MU1_MUA, i);
  }

  MU_EnableInterrupts(MU1_MUA, kMU_Rx0FullInterruptEnable | kMU_GenInt0InterruptEnable);
  NVIC_SetPriority(MU1_IRQn, 5);
  NVIC_EnableIRQ(MU1_IRQn);

  is_msg_init = true;
}
#endif


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
    cliPrintf("msg        : tx %d  rx %d  tx timeout %d  q full %d  q %d\n",
              (int)msg_tx_cnt, (int)msg_rx_cnt, (int)msg_tx_timeout, (int)msg_q_full,
              (int)ipcMsgAvailable());
    cliPrintf("stream     : m2p %d B  p2m %d B  doorbell %d  tx timeout %d\n",
              (int)((shared.m2p.head - shared.m2p.tail) & (SHARED_RING_SIZE - 1)),
              (int)((shared.p2m.head - shared.p2m.tail) & (SHARED_RING_SIZE - 1)),
              (int)stream_event_cnt, (int)stream_tx_timeout);
    ret = true;
  }

  if (args->argc >= 1 && args->isStr(0, "ping"))
  {
    uint32_t cnt = (args->argc == 2) ? (uint32_t)args->getData(1) : 1;
    uint32_t ok  = 0;
    uint32_t rtt, rtt_min = 0xFFFFFFFFU, rtt_max = 0, rtt_sum = 0;

    for (uint32_t i = 0; i < cnt; i++)
    {
      if (ipcPing(10, &rtt) == true)
      {
        ok++;
        rtt_sum += rtt;
        rtt_min  = (rtt < rtt_min) ? rtt : rtt_min;
        rtt_max  = (rtt > rtt_max) ? rtt : rtt_max;
      }
    }
    cliPrintf("ping %d/%d", (int)ok, (int)cnt);
    if (ok > 0)
    {
      cliPrintf("  rtt min %d us  avg %d us  max %d us", (int)rtt_min, (int)(rtt_sum / ok), (int)rtt_max);
    }
    cliPrintf("\n");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("ipc info\n");
    cliPrintf("ipc ping [count]\n");
  }
}
#endif

#endif
