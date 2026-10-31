#include "ap.h"
#include "module.h"




void apInit(void)
{
  //-- 각 모듈의 init() 이 여기서 실행된다 (ap/modules, MODULE_DEF).
  //   스레드가 필요한 모듈은 그 안에서 threadCreate() 로 등록한다 (cli 등).
  //
  moduleInit();
}

void apMain(void)
{
  uint32_t pre_time;


  //-- 등록된 스레드를 모두 만든다. 스케줄러는 main.c 가 이미 돌리고 있다.
  //
  threadBegin();

  //-- 부팅 로그는 여기까지다 (log boot 로 다시 본다). 부팅이 끝났음을 알린다 (titan-mini 와 같다).
  //
  logBoot(false);
  eventPub(EVENT_BOOT_DONE, 0);

  pre_time = millis();
  while (1)
  {
    if (millis() - pre_time >= 500)
    {
      pre_time = millis();
      ledToggle(_DEF_LED2);
    }

    eventUpdate();            // 큐에 쌓인 이벤트를 구독자에게 뿌린다
    moduleUpdate();           // update() 를 등록한 모듈들
    delay(1);                 // main 스레드가 CPU 를 혼자 잡지 않게 (docs/28)
  }
}
