#include "ap.h"
#include "modules/cmd/cmd_task.h"


#ifdef _USE_HW_RTOS
static void cmdThread(void *arg);
#endif




void apInit(void)
{
  cmdTaskInit();

#ifdef _USE_HW_RTOS
  xTaskCreate(cmdThread, "cmd",
              _HW_DEF_THREAD_CMD_STACK / sizeof(StackType_t), NULL,
              _HW_DEF_THREAD_CMD_PRI, NULL);
#endif
}

void apMain(void)
{
  //-- 살아 있다는 표시(10 ms). LED 와 UART 는 CM33 이 소유한다.
  //
  while (1)
  {
    ipcUpdate();
#ifdef _USE_HW_RTOS
    delay(10);
#else
    cmdTaskUpdate();
#endif
  }
}

#ifdef _USE_HW_RTOS
/*
 * CM33 에서 오는 cmd 패킷. 링에 쓰고 doorbell 을 울리면 깨어서(ipcStreamWait) 다 읽는다.
 * 기다리는 동안은 CPU 를 쓰지 않는다. 100 ms 마다는 doorbell 이 없어도 한 번 본다.
 */
void cmdThread(void *arg)
{
  (void)arg;

  while (1)
  {
    if (ipcStreamWait(100))
    {
      while (cmdTaskUpdate());
    }
  }
}
#endif
