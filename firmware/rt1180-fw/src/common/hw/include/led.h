#ifndef LED_H_
#define LED_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_LED
#define LED_MAX_CH  HW_LED_MAX_CH


bool ledInit(void);
void ledOn(uint8_t ch);
void ledOff(uint8_t ch);
void ledToggle(uint8_t ch);
void ledSetDuty(uint8_t ch, uint8_t duty);    // 0 ~ 100 %
uint8_t ledGetDuty(uint8_t ch);

#endif

#ifdef __cplusplus
}
#endif

#endif
