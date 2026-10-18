#ifndef RTOS_H_
#define RTOS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "def.h"

/*
 * bsp.h 가 이 파일을 무조건 include 한다 (titan-mini 와 같다).
 *
 * _USE_HW_RTOS 로 감싸지 않는 이유는 include 순서 때문이다. hw_def.h 는 맨 위에서
 * bsp.h 를 include 한 뒤에야 _USE_HW_RTOS 를 정의한다. 여기서 그 매크로를 보려고
 * 하면 아직 정의되기 전이라 FreeRTOS 헤더가 빠지고, log.c 처럼 뮤텍스를 쓰는
 * 드라이버가 SemaphoreHandle_t 를 못 찾는다.
 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"


bool rtosInit(void);
bool rtosIsRunning(void);


#ifdef __cplusplus
}
#endif

#endif
