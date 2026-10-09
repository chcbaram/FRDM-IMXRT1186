#ifndef BUTTON_H_
#define BUTTON_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_BUTTON

#define BUTTON_MAX_CH       HW_BUTTON_MAX_CH


// 소비자(스레드)별 입력 스냅샷.
//
//   드라이버는 상태와 누적 카운터만 발행하고, 엣지는 각 소비자가 자기
//   마지막 스냅샷과 비교해서 만든다. 그래서
//     - 오래된 이벤트가 남아있지 않고 (엣지는 항상 "내 지난 갱신 이후")
//     - 소비자끼리 이벤트를 뺏지 않는다.
//
//   버퍼는 스레드마다 하나씩 가져야 한다. 여러 스레드가 같은 버퍼를
//   공유하면 안된다.
//
typedef struct
{
  uint32_t data;                            // 현재 눌림 비트맵
  uint32_t pressed;                         // 이번 갱신에서 눌린 비트맵
  uint32_t released;                        // 이번 갱신에서 떨어진 비트맵
  uint32_t press_time[BUTTON_MAX_CH];       // 눌림 유지 시간(뗀 뒤에는 눌렸던 시간) ms
  uint32_t release_time[BUTTON_MAX_CH];     // 떨어진 뒤 경과 시간 ms
  uint16_t repeat[BUTTON_MAX_CH];           // 이번 갱신에서 발생한 리피트 횟수

  uint16_t press_cnt[BUTTON_MAX_CH];        // 이하 내부용
  uint16_t release_cnt[BUTTON_MAX_CH];
  uint16_t repeat_cnt[BUTTON_MAX_CH];
  uint32_t hold_ms[BUTTON_MAX_CH];
  uint32_t hold_done;
  bool     is_init;
} button_input_t;


bool     buttonInit(void);

// 현재 상태 조회. 어느 스레드에서 호출해도 된다.
//
bool     buttonGetPressed(uint8_t ch);
uint32_t buttonGetData(void);                     // 전체 채널의 눌림 상태 비트맵
uint8_t  buttonGetPressedCount(void);             // 동시에 눌린 버튼 개수
uint32_t buttonGetPressedTime(uint8_t ch);        // 눌림 유지 시간 ms
uint32_t buttonGetReleasedTime(uint8_t ch);       // 떨어진 뒤 경과 시간 ms

// 누적 카운터. 증가만 하며 초기화되지 않는다. 스냅샷을 쓰지 않는 곳에서
// 직접 delta 를 비교할 때 사용한다.
//
uint32_t buttonGetPressCount(uint8_t ch);
uint32_t buttonGetRepeatCount(uint8_t ch);

// 오토 리피트 (메뉴 스크롤 등)
//   detect_ms       : 누른 뒤 첫 리피트까지의 시간
//   repeat_delay_ms : 첫 리피트 후 두번째 리피트까지의 시간
//   repeat_ms       : 그 이후의 리피트 주기
//
void     buttonSetRepeatTime(uint8_t ch, uint32_t detect_ms, uint32_t repeat_delay_ms, uint32_t repeat_ms);

const char *buttonGetName(uint8_t ch);


// 스냅샷 API
//   루프마다 buttonInputUpdate() 를 한번 호출하고, 나머지 조회는 그
//   시점의 값으로 처리한다. (엣지와 시간의 짝이 맞는다)
//
bool     buttonInputInit(button_input_t *p_in);
bool     buttonInputUpdate(button_input_t *p_in);

bool     buttonInputGetPressed(button_input_t *p_in, uint8_t ch);     // 눌림 엣지
bool     buttonInputGetReleased(button_input_t *p_in, uint8_t ch);    // 뗌 엣지
bool     buttonInputGetHold(button_input_t *p_in, uint8_t ch, uint32_t hold_ms); // 롱프레스, 누를때마다 1회
uint32_t buttonInputGetRepeat(button_input_t *p_in, uint8_t ch);      // 리피트 발생 횟수
bool     buttonInputGetLevel(button_input_t *p_in, uint8_t ch);       // 스냅샷 시점의 눌림 상태
uint32_t buttonInputGetPressedTime(button_input_t *p_in, uint8_t ch);
uint32_t buttonInputGetReleasedTime(button_input_t *p_in, uint8_t ch);


#endif

#ifdef __cplusplus
}
#endif



#endif
