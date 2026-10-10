#include "ipc.h"

#ifdef _USE_HW_IPC
#include "shared.h"
#include <string.h>


/*
 * 상대 코어(CM7) 쪽 — titan-mini 의 cm33 ipc.c 와 같다.
 *
 * 자기가 살아 있다는 것만 알린다. 기동은 CM33 이 한다.
 * 공유 블록은 NOLOAD 라 시작 시 0 으로 밀리지 않는다. 자기 필드는 자기가 잡고
 * magic 을 마지막에 쓴다.
 */
__attribute__((section(".shared"), used))
static shared_t shared;


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

  __DMB();
  shared.magic = SHARED_MAGIC;

  return true;
}

void ipcUpdate(void)
{
  shared.peer_alive++;
  shared.peer_tick = millis();
}

#endif
