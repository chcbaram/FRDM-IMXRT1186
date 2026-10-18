#include "main.h"


#ifdef _USE_HW_RTOS

static void mainThread(void *arg);


int main(void)
{
  bspInit();

  //-- CM33 과 같다. main 스레드 하나만 만들고 스케줄러를 시작한다 (docs/28).
  //   힙이 모자라 실패하면 alive 가 서지 않아 CM33 이 TIMEOUT 으로 안다.
  //
  if (xTaskCreate(mainThread,
                  "main",
                  _HW_DEF_THREAD_MAIN_STACK / sizeof(StackType_t),
                  NULL,
                  _HW_DEF_THREAD_MAIN_PRI,
                  NULL) == pdPASS)
  {
    vTaskStartScheduler();
  }

  while (1);
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
