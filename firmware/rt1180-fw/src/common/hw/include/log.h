#ifndef LOG_H_
#define LOG_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_LOG

#define LOG_CH            HW_LOG_CH
#define LOG_BOOT_BUF_MAX  HW_LOG_BOOT_BUF_MAX
#define LOG_LIST_BUF_MAX  HW_LOG_LIST_BUF_MAX


bool logInit(void);
void logEnable(void);
void logDisable(void);
bool logOpen(uint8_t ch, uint32_t baud);
bool logIsOpen(void);
void logBoot(uint8_t enable);
void logPrintf(const char *fmt, ...);

//-- 쌓인 부팅 로그를 offset 부터 읽는다. 읽은 바이트 수(끝이면 0).
//   UART 가 없는 상대 코어(CM7)가 구현하고, 주 코어가 cmd 패킷으로 가져가 찍는다 (docs/28).
uint32_t logReadBoot(uint32_t offset, uint8_t *p_data, uint32_t length);

#endif

#ifdef __cplusplus
}
#endif



#endif