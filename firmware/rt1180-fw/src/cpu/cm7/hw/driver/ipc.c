#include "ipc.h"

#ifdef _USE_HW_IPC
#include "shared.h"
#include "qbuffer.h"
#include "fsl_mu.h"
#include <string.h>


/*
 * 상대 코어(CM7) 쪽 — titan-mini 의 cm33 ipc.c 와 같다.
 *
 * 자기가 살아 있다는 것만 알린다. 기동은 CM33 이 한다.
 * 공유 블록은 NOLOAD 라 시작 시 0 으로 밀리지 않는다. 자기 필드는 자기가 잡고
 * magic 을 마지막에 쓴다.
 *
 * 메시지는 MU1 의 B 쪽(MU1_MUB)을 쓴다. 형식은 shared.h 3절.
 * PING 은 인터럽트 안에서 바로 PONG 으로 돌려준다. 나머지는 큐에 쌓는다.
 */
__attribute__((section(".shared"), used))
static shared_t shared;


static void ipcMsgInit(void);
static void ringCopyIn(uint8_t *p_ring, uint32_t pos, const uint8_t *p_src, uint32_t length);
static void ringCopyOut(uint8_t *p_dst, const uint8_t *p_ring, uint32_t pos, uint32_t length);


static bool      is_msg_init = false;
static qbuffer_t msg_q;
static shared_ring_t * const p_tx = &shared.p2m;
static shared_ring_t * const p_rx = &shared.m2p;
static volatile uint32_t stream_event_cnt = 0;
static ipc_msg_t msg_q_buf[HW_IPC_MSG_Q_MAX];


bool ipcInit(void)
{
  shared.peer_alive = 0;
  shared.peer_tick  = 0;
  shared.version    = SHARED_VERSION;

  shared.peer_clock = SystemCoreClock;
  strncpy((char *)shared.peer_name,   _DEF_BOARD_NAME,        SHARED_NAME_MAX - 1);
  strncpy((char *)shared.peer_fw_ver, _DEF_FIRMWATRE_VERSION, SHARED_VER_MAX  - 1);
  shared.peer_name[SHARED_NAME_MAX - 1]  = 0;
  shared.peer_fw_ver[SHARED_VER_MAX - 1] = 0;

  //-- magic 을 쓰기 전에 MU 를 연다. CM33 은 magic 을 보고 나서 메시지를 보낸다.
  //
  ipcMsgInit();

  __DMB();
  shared.magic = SHARED_MAGIC;

  return true;
}

void ipcUpdate(void)
{
  shared.peer_alive++;
  shared.peer_tick = millis();
}

bool ipcMsgSend(uint32_t id, uint32_t data, uint32_t timeout_ms)
{
  const uint32_t tx_empty = kMU_Tx0EmptyFlag | kMU_Tx1EmptyFlag;
  uint32_t       pre_ms;
  uint32_t       primask;

  if (is_msg_init != true || id == 0)
    return false;

  pre_ms = millis();
  while ((MU_GetStatusFlags(MU1_MUB) & tx_empty) != tx_empty)
  {
    if ((millis() - pre_ms) >= timeout_ms)
      return false;
  }

  //-- data 다음 id. 인터럽트(PONG 응답)가 두 쓰기 사이에 끼지 않게 막는다.
  //
  primask = __get_PRIMASK();
  __disable_irq();
  MU_SendMsgNonBlocking(MU1_MUB, 1, data);
  MU_SendMsgNonBlocking(MU1_MUB, 0, id);
  __set_PRIMASK(primask);

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
    (void)MU_TriggerInterrupts(MU1_MUB, kMU_GenInt0InterruptTrigger);
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

  if ((MU_GetStatusFlags(MU1_MUB) & kMU_GenInt0Flag) != 0)
  {
    MU_ClearStatusFlags(MU1_MUB, kMU_GenInt0Flag);
    stream_event_cnt++;
  }

  while ((MU_GetStatusFlags(MU1_MUB) & kMU_Rx0FullFlag) != 0)
  {
    msg.data = MU_ReceiveMsgNonBlocking(MU1_MUB, 1);
    msg.id   = MU_ReceiveMsgNonBlocking(MU1_MUB, 0);

    if (msg.id == SHARED_MSG_PING)
    {
      ipcMsgSend(SHARED_MSG_PONG, msg.data, 1);
    }
    else
    {
      qbufferWrite(&msg_q, (uint8_t *)&msg, 1);
    }
  }
  __DSB();
}

static void ipcMsgInit(void)
{
  qbufferCreateBySize(&msg_q, (uint8_t *)msg_q_buf, sizeof(ipc_msg_t), HW_IPC_MSG_Q_MAX);

  MU_Init(MU1_MUB);
  for (uint32_t i = 0; i < MU_RR_COUNT; i++)
  {
    (void)MU_ReceiveMsgNonBlocking(MU1_MUB, i);
  }

  MU_EnableInterrupts(MU1_MUB, kMU_Rx0FullInterruptEnable | kMU_GenInt0InterruptEnable);
  NVIC_SetPriority(MU1_IRQn, 5);
  NVIC_EnableIRQ(MU1_IRQn);

  is_msg_init = true;
}

#endif
