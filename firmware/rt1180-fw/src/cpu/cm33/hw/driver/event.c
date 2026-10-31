#include "event.h"

#ifdef _USE_HW_EVENT

#include "qbuffer.h"
#include "cli.h"
#include "log.h"

/*
 * 이벤트 — titan-mini 의 event.c 를 그대로 가져왔다 (docs/29-event.md).
 *
 * eventPub() 은 큐에 넣기만 하고, main 스레드의 eventUpdate() 가 구독자에게 뿌린다.
 * 이 보드에서는 cli · button · main 스레드가 모두 발행하므로 큐에 넣는 곳을 임계구간으로
 * 감쌌다 (원본은 보호가 없다). ISR 에서는 부르지 않는다.
 */


#define EVENT_Q_MAX       HW_EVENT_Q_MAX
#define EVENT_NODE_MAX    HW_EVENT_NODE_MAX


typedef struct
{
  event_func_t func;
  const char  *p_name;
} event_node_t;


#if CLI_USE(HW_EVENT)
static void cliEvent(cli_args_t *args);
#endif

static bool         is_init = false;
static bool         is_log  = true;
static qbuffer_t    event_q;
static event_t      event_buf[EVENT_Q_MAX];
static event_node_t event_node[EVENT_NODE_MAX];
static int32_t      node_cnt = 0;
static uint32_t     pub_cnt  = 0;
static uint32_t     drop_cnt = 0;




bool eventInit(void)
{
  node_cnt = 0;
  pub_cnt  = 0;
  drop_cnt = 0;

  for (int i = 0; i < EVENT_NODE_MAX; i++)
  {
    event_node[i].func   = NULL;
    event_node[i].p_name = NULL;
  }

  is_init = qbufferCreateBySize(&event_q, (uint8_t *)event_buf, sizeof(event_t), EVENT_Q_MAX);

#if CLI_USE(HW_EVENT)
  cliAdd("event", cliEvent);
#endif

  return is_init;
}

bool eventSubFunc(const char *p_name, event_func_t sub_func)
{
  if (node_cnt >= EVENT_NODE_MAX) return false;
  if (sub_func == NULL) return false;

  event_node[node_cnt].func   = sub_func;
  event_node[node_cnt].p_name = p_name;
  node_cnt++;

  return true;
}

bool eventPubFunc(const char *p_name, EventCode_t event_code, uint32_t event_data)
{
  event_t event_msg;

  if (is_init != true) return false;

  event_msg.code   = event_code;
  event_msg.data   = event_data;
  event_msg.p_name = p_name;

#ifdef _USE_HW_RTOS
  taskENTER_CRITICAL();
#endif
  bool ret = qbufferWrite(&event_q, (uint8_t *)&event_msg, 1);
#ifdef _USE_HW_RTOS
  taskEXIT_CRITICAL();
#endif

  if (ret != true)
  {
    //-- 큐가 넘치면 조용히 버리지 않고 센다. event info 로 확인할 수 있다.
    //
    drop_cnt++;
    return false;
  }

  pub_cnt++;
  return true;
}

bool eventUpdate(void)
{
  if (is_init != true) return false;

  while (qbufferAvailable(&event_q) > 0)
  {
    event_t evt;

    if (qbufferRead(&event_q, (uint8_t *)&evt, 1) != true) break;

    if (is_log == true)
    {
      logPrintf("[  ] Event %s : %d\r\n", evt.p_name, (int)evt.data);
    }

    for (int i = 0; i < node_cnt; i++)
    {
      if (event_node[i].func != NULL)
      {
        event_node[i].func(&evt);
      }
    }
  }

  return true;
}


#if CLI_USE(HW_EVENT)
void cliEvent(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("sub   : %d / %d\n", (int)node_cnt, EVENT_NODE_MAX);
    cliPrintf("queue : %d / %d\n", (int)qbufferAvailable(&event_q), EVENT_Q_MAX);
    cliPrintf("pub   : %d\n", (int)pub_cnt);
    cliPrintf("drop  : %d\n", (int)drop_cnt);
    cliPrintf("log   : %s\n", is_log ? "on" : "off");
    cliPrintf("\n");

    for (int i = 0; i < node_cnt; i++)
    {
      cliPrintf("%d : %s\n", i, event_node[i].p_name);
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "log"))
  {
    is_log = args->isStr(1, "on");
    cliPrintf("event log %s\n", is_log ? "on" : "off");
    ret = true;
  }

  //-- 구독자 동작을 확인할 때 쓴다. 예) event pub 3 1  (EVENT_ETH_LINK up)
  //
  if (args->argc == 3 && args->isStr(0, "pub"))
  {
    EventCode_t code = (EventCode_t)args->getData(1);
    uint32_t    data = (uint32_t)args->getData(2);

    if (code < EVENT_MAX)
    {
      eventPubFunc("cli", code, data);
      cliPrintf("pub code %d data %d\n", (int)code, (int)data);
    }
    else
    {
      cliPrintf("code 는 0 ~ %d\n", EVENT_MAX - 1);
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("event info\n");
    cliPrintf("event log on:off\n");
    cliPrintf("event pub code[0~%d] data\n", EVENT_MAX - 1);
  }
}
#endif

#endif
