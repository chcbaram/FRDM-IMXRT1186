#ifndef HW_H_
#define HW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#include "led.h"
#include "qspi.h"
#include "rtc.h"
#include "reset.h"
#include "uart.h"
#include "cli.h"
#include "log.h"
#include "hw_info.h"


bool hwInit(void);


#ifdef __cplusplus
}
#endif

#endif
