#include "bsp.h"
#include "clock.h"


static volatile uint32_t systick_ms = 0;




bool bspInit(void)
{
  //-- 클럭 루트를 PLL 로 옮긴다 (앱의 bsp/clock.c 를 같이 쓴다). 앱은 점프 뒤에
  //   같은 clockInit() 을 한 번 더 부른다 (docs/40 8절).
  //
  clockInit();

  //-- RTOS 가 없으므로 SysTick 이 1 ms 틱이다. MPU · XCACHE_PS 는 켜지 않는다.
  //   부트로더는 Code TCM · System TCM 만 쓰고, 점프할 때 되돌릴 것도 줄어든다 (docs/40 5절).
  //
  SysTick_Config(SystemCoreClock / 1000);

  return true;
}

void SysTick_Handler(void)
{
  systick_ms++;
}

void delay(uint32_t ms)
{
  uint32_t pre_time = systick_ms;

  while (systick_ms - pre_time < ms);
}

uint32_t millis(void)
{
  return systick_ms;
}

uint32_t micros(void)
{
  uint32_t m0, m1, val;

  do
  {
    m0  = millis();
    val = SysTick->VAL;
    m1  = millis();
  } while (m0 != m1);

  uint32_t load = SysTick->LOAD + 1;

  return m0 * 1000 + ((load - val) * 1000) / load;
}

/*
 * 앱으로 넘어간다 (docs/40 5절, docs/42). 돌아오지 않는다.
 *
 * - 인터럽트 : 남은 인터럽트가 앱이 VTOR 를 옮기기 전에 뜨면 부트로더(TCM) 핸들러로 간다.
 *              SysTick 과 NVIC 를 모두 끄고 대기도 지운다. PRIMASK 는 막은 채로 넘긴다.
 *              앱 startup 이 .data/.bss 를 마친 뒤 cpsie i 로 연다.
 * - XCACHE_PC : 부트로더가 방금 앱을 썼을 수 있다(43). 코드 버스 캐시의 옛 내용을 버린다.
 * - MSPLIM · CONTROL : 부트로더 스택 하한이 남아 있으면 앱 스택에서 STKOF 폴트가 난다.
 * - MSP · PC : 앱 벡터 [0] · [1]. MSP 를 바꾼 뒤에는 스택을 쓰면 안 되므로 어셈블리로 넘긴다.
 *
 * MPU · XCACHE_PS 는 부트로더가 켜지 않았으므로 되돌릴 것이 없다 (docs/41 4절).
 */
void bspJumpToApp(uint32_t vector_addr)
{
  uint32_t msp   = *(volatile uint32_t *)(vector_addr + 0);
  uint32_t entry = *(volatile uint32_t *)(vector_addr + 4);


  __disable_irq();

  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;
  SCB->ICSR     = SCB_ICSR_PENDSTCLR_Msk;

  for (uint32_t i = 0; i < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]); i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFF;
    NVIC->ICPR[i] = 0xFFFFFFFF;
  }
  __DSB();
  __ISB();

  XCACHE_PC->CCR |= XCACHE_CCR_INVW0_MASK | XCACHE_CCR_INVW1_MASK | XCACHE_CCR_GO_MASK;
  while (XCACHE_PC->CCR & XCACHE_CCR_GO_MASK);

  __set_MSPLIM(0);
  __set_CONTROL(0);
  SCB->VTOR = vector_addr;
  __DSB();
  __ISB();

  __asm volatile ("msr msp, %0  \n"
                  "bx  %1       \n"
                  :
                  : "r" (msp), "r" (entry)
                  : "memory");

  while (1);
}

void Error_Handler(void)
{
  if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)
  {
    __BKPT(0);
  }

  __disable_irq();
  while (1)
  {
  }
}
