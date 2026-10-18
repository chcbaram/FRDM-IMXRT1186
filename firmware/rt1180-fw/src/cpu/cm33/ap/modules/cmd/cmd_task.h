#ifndef CMD_TASK_H_
#define CMD_TASK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


#ifdef _USE_HW_CMD

bool cmdTaskInit(void);
bool cmdTaskUpdate(void);

//-- 채널을 잠근다. 백그라운드 수신(update)과 명령을 보내고 응답을 기다리는 쪽(CLI 의 peer)이
//   서로 다른 스레드라, 같은 링 버퍼를 동시에 읽으면 응답을 가로챈다.
bool cmdTaskLock(uint32_t timeout_ms);
void cmdTaskUnlock(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
