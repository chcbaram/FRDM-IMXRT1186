#include "main.h"


#ifdef _USE_HW_RTOS

static void mainThread(void *arg);


int main(void)
{
  bspInit();

  //-- main 스레드 하나만 만들고 바로 스케줄러를 시작한다 (titan-mini 와 같다).
  //   hwInit / apInit / apMain 이 전부 스케줄러 아래에서 돌기 때문에
  //   millis() 와 delay() 가 처음부터 정상 동작한다 (docs/28-freertos.md).
  //
  if (xTaskCreate(mainThread,
                  "main",
                  _HW_DEF_THREAD_MAIN_STACK / sizeof(StackType_t),
                  NULL,
                  _HW_DEF_THREAD_MAIN_PRI,
                  NULL) != pdPASS)
  {
    //-- 여기 오면 힙이 모자란 것이다. 로그도 아직 없으니 LED 로 알린다.
    //
    ledInit();
    while (1)
    {
      ledToggle(_DEF_LED1);
      delay(100);
    }
  }

  vTaskStartScheduler();

  return 0;
}

void mainThread(void *arg)
{
  (void)arg;

  hwInit();
  apInit();
  apMain();
}

#else

int main(void)
{
  bspInit();

  hwInit();
  apInit();
  apMain();

  return 0;
}

#endif
