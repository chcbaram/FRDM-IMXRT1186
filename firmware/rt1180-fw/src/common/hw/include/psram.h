#ifndef PSRAM_H_
#define PSRAM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_PSRAM


bool     psramInit(void);
bool     psramIsInit(void);
uint32_t psramGetAddr(void);
uint32_t psramGetLength(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
