#include "ap.h"
#include "module.h"




void apInit(void)
{
  //-- 각 모듈의 init() 이 여기서 실행된다 (ap/modules, MODULE_DEF). CM33 과 같다.
  //   스레드가 필요한 모듈은 그 안에서 threadCreate() 로 등록한다 (cmd).
  //
  moduleInit();
}

void apMain(void)
{
  //-- 등록된 스레드를 모두 만든다. 스케줄러는 main.c 가 이미 돌리고 있다.
  //
  threadBegin();

  //-- 살아 있다는 표시(10 ms). LED 와 UART 는 CM33 이 소유한다.
  //
  while (1)
  {
    ipcUpdate();
    moduleUpdate();
    delay(10);
  }
}
