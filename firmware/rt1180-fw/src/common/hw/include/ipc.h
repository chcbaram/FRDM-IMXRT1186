#ifndef IPC_H_
#define IPC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_IPC


/*
 * 코어간 연동(IPC) - 상대 코어의 기동과 생존 확인.
 *
 * 이 헤더는 common/ 에 있으므로 MCU 에 중립이어야 한다. "상대 코어(peer)" 라고만
 * 부르고 CPU1 / CM4 / core1 같은 벤더 용어는 쓰지 않는다. 실제로 어느 코어를
 * 어떻게 깨우는지는 hw/driver 쪽 구현이 안다.
 *
 * 공유 블록의 실제 모양(shared.h)과 전역 변수는 여기서 드러내지 않는다.
 * 바깥은 아래 함수로만 접근한다.
 *
 * 기동 성공 여부를 알려주는 API 는 보통 벤더 SDK 에 없다. 그래서 공유 블록(shared.h)을
 * 통한 핸드셰이크로 직접 확인한다. (titan-mini 와 같은 설계)
 *
 *   주 코어   magic 을 0 으로 지운다 -> 기동 요청 -> magic 이 설 때까지 기다린다
 *   상대 코어 자기 필드 초기화 -> __DMB() -> magic 을 마지막에 쓴다
 *
 * magic 을 먼저 지우는 것이 핵심이다. 공유 블록은 NOLOAD 라 시작 시 0 으로 밀리지
 * 않고 SRAM 은 주 코어 리셋만으로 내용이 남는다. 지우지 않으면 상대 코어가 없어도
 * 이전 세션이 써 둔 magic 이 유효해 보인다.
 */
typedef enum
{
  IPC_STATE_DISABLED = 0,    // 상대 코어 이미지를 함께 빌드하지 않았다. 기동하지 않는다.
  IPC_STATE_NO_IMAGE,        // 이미지 자리에 유효한 이미지가 없다 (헤더 magic/크기/CRC).
  IPC_STATE_START_FAIL,      // 기동 요청 자체가 실패했다 (보안 엔진 응답 등).
  IPC_STATE_TIMEOUT,         // 기동을 요청했으나 핸드셰이크가 오지 않았다.
  IPC_STATE_BAD_VERSION,     // 응답했지만 공유 블록 버전이 다르다. 한쪽만 다시 빌드한 경우.
  IPC_STATE_RUNNING,         // 정상.
} IpcState_t;


bool         ipcInit(void);

IpcState_t   ipcGetState(void);
const char  *ipcGetStateStr(void);

//-- 부팅 시 핸드셰이크가 성공했는지. 기동 시점에 확정되고 이후 바뀌지 않는다.
bool         ipcIsBooted(void);

//-- 상대 코어가 지금 살아 있는지. alive 카운터가 최근에 움직였는지로 판단한다.
//   블로킹하지 않으므로 주기적으로 불러도 된다.
bool         ipcIsRunning(void);

uint32_t     ipcGetBootTime(void);
uint32_t     ipcGetAliveCnt(void);
uint32_t     ipcGetTick(void);

//-- 상대 코어가 부팅하며 알려준 자기 정보. 부팅에 성공했을 때만 유효하다.
const char  *ipcGetName(void);
const char  *ipcGetVersion(void);
uint32_t     ipcGetClock(void);            // Hz

//-- 상대 코어가 자기 생존을 알린다. 상대 코어 쪽에서만 구현한다.
void         ipcUpdate(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
