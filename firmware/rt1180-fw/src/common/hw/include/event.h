#ifndef EVENT_H_
#define EVENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_EVENT


typedef struct
{
  EventCode_t code;
  uint32_t    data;
  const char *p_name;
} event_t;

typedef bool (*event_func_t)(event_t *);


//-- 매크로로 감싸는 이유는 코드 이름을 문자열로 같이 넘기기 위해서다.
//   로그와 CLI 에서 숫자 대신 이름이 보인다.
//
#define eventPub(event_code, event_data)  eventPubFunc(#event_code, event_code, event_data)
#define eventSub(sub_func)                eventSubFunc(#sub_func, sub_func)


bool eventInit(void);

//-- 쌓인 이벤트를 구독자에게 뿌린다. 스레드 문맥에서 주기적으로 부른다.
//
bool eventUpdate(void);

bool eventSubFunc(const char *p_name, event_func_t sub_func);
bool eventPubFunc(const char *p_name, EventCode_t event_code, uint32_t event_data);


#endif

#ifdef __cplusplus
}
#endif

#endif
