#ifndef CM7_H_
#define CM7_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "def.h"


typedef enum
{
  CM7_OK = 0,
  CM7_ERR_ELE_BUSY,         // ELE 메일박스가 비지 않는다
  CM7_ERR_ELE_TIMEOUT,      // ELE 응답이 오지 않는다
  CM7_ERR_ELE_RESP,         // ELE 가 실패를 돌려줬다
} cm7_err_t;


void      cm7Prepare(void);
void      cm7Load(const void *p_src, uint32_t size);
cm7_err_t cm7Start(void);
uint32_t  cm7GetEleResp(uint32_t index);


#ifdef __cplusplus
}
#endif

#endif
