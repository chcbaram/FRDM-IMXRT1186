#ifndef BSP_H_
#define BSP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "def.h"
#include "fsl_device_registers.h"
#include <assert.h>
//-- 부트로더(rt1180-boot)는 이 폴더의 clock.c 를 같이 쓰는데 FreeRTOS 가 없다. 따옴표 include 는
//   clock.c 옆의 이 bsp.h 를 먼저 잡으므로 rtos.h 가 있을 때만 넣는다.
#if __has_include("rtos.h")
#include "rtos.h"
#endif


bool bspInit(void);
void mpuInit(void);

void     delay(uint32_t time_ms);
uint32_t millis(void);
uint32_t micros(void);

void Error_Handler(void);


#ifdef __cplusplus
}
#endif

#endif
