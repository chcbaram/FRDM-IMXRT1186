#ifndef THREAD_H_
#define THREAD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_THREAD

#define THREAD_MAX_CNT      HW_THREAD_MAX_CNT


typedef int16_t thread_id_t;

typedef void (*thread_func_t)(void *arg);


bool threadInit(void);

//-- 스레드를 등록만 해 둔다. 실제 생성은 threadBegin() 에서 한다.
//   hwInit() 단계에서 드라이버들이 자기 스레드를 등록할 수 있게 하려는 것이다.
//
thread_id_t threadCreate(const char   *name,
                         thread_func_t func,
                         void         *arg,
                         uint32_t      priority,
                         uint32_t      stack_bytes);

//-- 등록된 스레드를 전부 만들고 스케줄러를 시작한다. 돌아오지 않는다.
//
bool threadBegin(void);

uint32_t threadGetCount(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
