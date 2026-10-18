#ifndef PSRAM_H_
#define PSRAM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_PSRAM

/*
 * 큰 버퍼를 이 코어의 PSRAM 몫에 둔다. 0 으로 채워지는 것은 psramInit() 이후다.
 *
 *   static uint8_t frame_buf[512 * 1024] PSRAM_BSS;
 *
 * psramGetAddr() / psramGetLength() 는 이 코어 몫(링커 PSRAM 영역)이다.
 */
#define PSRAM_BSS   __attribute__((section(".psram_bss"), aligned(32)))


bool     psramInit(void);
bool     psramIsInit(void);
uint32_t psramGetAddr(void);
uint32_t psramGetLength(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
