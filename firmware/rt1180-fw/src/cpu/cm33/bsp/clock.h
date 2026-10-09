#ifndef CLOCK_H_
#define CLOCK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "def.h"


typedef struct
{
  const char *name;
  uint32_t    mux;
  uint32_t    div;
  uint32_t    calc_hz;      // CCM 레지스터로 계산한 값
  uint32_t    meas_hz;      // CCM OBSERVE 로 측정한 값 (0 = 측정 안 함)
} clock_info_t;


bool     clockInit(void);
bool     clockIsApplied(void);
uint32_t clockGetRootCount(void);
bool     clockGetRootInfo(uint32_t index, clock_info_t *p_info);
bool     clockGetPllInfo(uint32_t index, const char **p_name, bool *p_enable, bool *p_bypass, uint32_t *p_hz);
uint32_t clockGetPllCount(void);
bool     clockIsOsc24mOn(void);


#ifdef __cplusplus
}
#endif

#endif
