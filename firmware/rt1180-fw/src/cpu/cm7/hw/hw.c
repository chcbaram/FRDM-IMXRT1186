#include "hw.h"


bool hwInit(void)
{
  //-- 공유 블록을 잡고 CM33 에 살아 있음을 알린다.
  //   magic 이 여기서 서면 CM33 의 기동 대기가 풀린다.
  //
  ipcInit();

  return true;
}
