#include "button.h"


#ifdef _USE_HW_BUTTON
#include "cli.h"
#include "log.h"
#include "swtimer.h"
#include "fsl_rgpio.h"
#include "fsl_iomuxc.h"
#include "fsl_clock.h"


#define NAME_DEF(x)  x, #x

// 채터링 제거 시간. 이 시간동안 같은 값이 유지되어야 상태를 확정한다.
//
#define BUTTON_DEBOUNCE_TIME      10
#define BUTTON_UPDATE_TIME        1

#define BUTTON_BIT(ch)            (1U<<(ch))


typedef struct
{
  RGPIO_Type     *port;
  uint32_t        pin;
  uint32_t        mux[5];                   // IOMUXC_GPIO_xxx_GPIOn_IOmm
  uint32_t        pad;                      // SW_PAD_CTL 값
  uint8_t         on_state;

  ButtonPinName_t pin_name;
  const char     *p_name;
} button_pin_t;

// 드라이버가 발행하는 상태.
//   sticky 이벤트 플래그는 두지 않는다. 엣지는 소비자가 카운터 delta 로
//   만들기 때문에, 언제 읽어도 "내가 마지막으로 본 이후" 만 남는다.
//
typedef struct
{
  uint32_t data;                            // 눌림 비트맵
  uint16_t press_cnt[BUTTON_MAX_CH];        // 누적 눌림 횟수
  uint16_t release_cnt[BUTTON_MAX_CH];      // 누적 뗌 횟수
  uint16_t repeat_cnt[BUTTON_MAX_CH];       // 누적 리피트 횟수
  uint32_t press_time[BUTTON_MAX_CH];
  uint32_t release_time[BUTTON_MAX_CH];
} button_pub_t;

// 버튼 스레드만 접근하는 내부 상태
//
typedef struct
{
  bool     pressed;
  uint32_t press_start_time;
  uint32_t release_start_time;

  uint32_t repeat_stage;                    // 0:최초, 1:첫 리피트 후, 2~:반복
  uint32_t repeat_pre_time;
  uint32_t repeat_time_detect;
  uint32_t repeat_time_delay;
  uint32_t repeat_time;

  bool     pin_pre;
  uint32_t debounce_pre_time;
} button_t;


#if CLI_USE(HW_BUTTON)
static void cliButton(cli_args_t *args);
#endif
static void buttonISR(void *arg);
static void buttonUpdate(void);
static void buttonPublish(void);
static void buttonCopy(button_pub_t *p_dst);
static bool buttonGetPin(uint8_t ch);


// FRDM-IMXRT1186 : SW4 (GPIO Interrupt Button) — 회로도 4페이지
//
//   GPIO_AD_12 → RGPIO4.12, J30 1-2 (기본)
//   누르면 GND 로 연결된다. 외부 풀업 R279(100K)는 DNP 라서 내부 풀업을 쓴다.
//   → active low, PAD = PUE | PUS (0x0C)
//
static const button_pin_t button_pin[BUTTON_MAX_CH] =
{
  {RGPIO4, 12, {IOMUXC_GPIO_AD_12_GPIO4_IO12},
   IOMUXC_SW_PAD_CTL_PAD_PUE_MASK | IOMUXC_SW_PAD_CTL_PAD_PUS_MASK, _DEF_LOW, NAME_DEF(BTN_USER)},
};

static button_t     button_tbl[BUTTON_MAX_CH];    // 버튼 스레드 전용
static button_pub_t button_pub;                   // 버튼 스레드 전용(발행 원본)
static button_pub_t button_shared;                // 공유본. 임계구간에서만 복사한다.
static bool is_init = false;




bool buttonInit(void)
{
  bool ret = true;
  rgpio_pin_config_t gpio_config = {kRGPIO_DigitalInput, 0};


  // 비트맵으로 채널을 다루므로 32채널까지만 지원한다.
  //
  assert(BUTTON_MAX_CH <= 32);

  //-- GPIO_AD 핀은 IOMUXC LPCG Iomuxc1 이다 (AON 핀은 Iomuxc2)
  //
  CLOCK_EnableClock(kCLOCK_Iomuxc1);

  for (int i=0; i<BUTTON_MAX_CH; i++)
  {
    const button_pin_t *p = &button_pin[i];

    RGPIO_PinInit(p->port, p->pin, &gpio_config);
    IOMUXC_SetPinMux(p->mux[0], p->mux[1], p->mux[2], p->mux[3], p->mux[4], 0U);
    IOMUXC_SetPinConfig(p->mux[0], p->mux[1], p->mux[2], p->mux[3], p->mux[4], p->pad);

    assert(i == (int)button_pin[i].pin_name);
  }

  memset(&button_pub, 0, sizeof(button_pub));

  for (int i=0; i<BUTTON_MAX_CH; i++)
  {
    button_t *p_btn = &button_tbl[i];

    p_btn->pressed            = false;
    p_btn->press_start_time   = millis();
    p_btn->release_start_time = millis();

    p_btn->repeat_stage       = 0;
    p_btn->repeat_pre_time    = millis();
    p_btn->repeat_time_detect = 60;
    p_btn->repeat_time_delay  = 250;
    p_btn->repeat_time        = 200;

    p_btn->pin_pre            = buttonGetPin(i);
    p_btn->debounce_pre_time  = millis();
  }
  buttonPublish();

  //-- RTOS 가 없으므로 원본(NUCLEO-C5A3ZG)의 1 ms 버튼 스레드 대신 swtimer 로
  //   SysTick 인터럽트에서 갱신한다. RTOS 를 붙이면(로드맵 28) 스레드로 되돌린다.
  //
  swtimer_handle_t h = swtimerGetHandle();
  if (h < 0)
  {
    ret = false;
  }
  else
  {
    swtimerSet(h, BUTTON_UPDATE_TIME, LOOP_TIME, buttonISR, NULL);
    swtimerStart(h);
  }

  is_init = ret;
  logPrintf("[%s] buttonInit()\n", ret ? "OK":"E_");

#if CLI_USE(HW_BUTTON)
  cliAdd("button", cliButton);
#endif

  return ret;
}

void buttonISR(void *arg)
{
  (void)arg;

  buttonUpdate();
  buttonPublish();
}

void buttonUpdate(void)
{
  uint32_t cur_time = millis();
  uint32_t repeat_time;


  for (int i=0; i<BUTTON_MAX_CH; i++)
  {
    button_t *p_btn = &button_tbl[i];
    bool pin;

    pin = buttonGetPin(i);

    // 채터링 제거. 같은 값이 BUTTON_DEBOUNCE_TIME 이상 유지되어야 확정한다.
    //
    if (pin != p_btn->pin_pre)
    {
      p_btn->pin_pre = pin;
      p_btn->debounce_pre_time = cur_time;
    }
    if (cur_time - p_btn->debounce_pre_time >= BUTTON_DEBOUNCE_TIME)
    {
      if (pin == true && p_btn->pressed != true)
      {
        p_btn->pressed          = true;
        p_btn->press_start_time = cur_time;
        p_btn->repeat_stage     = 0;
        p_btn->repeat_pre_time  = cur_time;

        button_pub.data |= BUTTON_BIT(i);
        button_pub.press_cnt[i]++;
      }
      if (pin != true && p_btn->pressed == true)
      {
        p_btn->pressed            = false;
        p_btn->release_start_time = cur_time;
        p_btn->repeat_stage       = 0;

        button_pub.data &= ~BUTTON_BIT(i);
        button_pub.release_cnt[i]++;
      }
    }

    if (p_btn->pressed == true)
    {
      button_pub.press_time[i] = cur_time - p_btn->press_start_time;

      // 오토 리피트
      //
      if (p_btn->repeat_stage == 0)
        repeat_time = p_btn->repeat_time_detect;
      else if (p_btn->repeat_stage == 1)
        repeat_time = p_btn->repeat_time_delay;
      else
        repeat_time = p_btn->repeat_time;

      if (repeat_time > 0 && (cur_time - p_btn->repeat_pre_time) >= repeat_time)
      {
        p_btn->repeat_pre_time = cur_time;
        p_btn->repeat_stage++;
        button_pub.repeat_cnt[i]++;
      }
    }
    else
    {
      button_pub.release_time[i] = cur_time - p_btn->release_start_time;
    }
  }
}

// 공유본 갱신/복사.
//   writer 는 SysTick 인터럽트(swtimer)이고 reader 는 메인 루프다. writer 는
//   reader 에게 선점되지 않으므로 그대로 복사하고, reader 쪽만 인터럽트를 막고
//   복사한다. (원본은 RTOS 임계구간 taskENTER_CRITICAL)
//
void buttonPublish(void)
{
  memcpy(&button_shared, &button_pub, sizeof(button_pub_t));
}

void buttonCopy(button_pub_t *p_dst)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  memcpy(p_dst, &button_shared, sizeof(button_pub_t));
  __set_PRIMASK(primask);
}

bool buttonGetPin(uint8_t ch)
{
  bool ret = false;

  if (ch >= BUTTON_MAX_CH)
  {
    return false;
  }

  if (RGPIO_PinRead(button_pin[ch].port, button_pin[ch].pin) == button_pin[ch].on_state)
  {
    ret = true;
  }

  return ret;
}


//-- 단일 값 조회. 정렬된 32bit(16bit) 접근이라 원자적이므로 락이 필요없다.
//
bool buttonGetPressed(uint8_t ch)
{
  if (ch >= BUTTON_MAX_CH)
  {
    return false;
  }

  return (button_shared.data & BUTTON_BIT(ch)) ? true : false;
}

uint32_t buttonGetData(void)
{
  return button_shared.data;
}

uint8_t buttonGetPressedCount(void)
{
  uint32_t data = button_shared.data;
  uint8_t ret = 0;


  for (int i=0; i<BUTTON_MAX_CH; i++)
  {
    if (data & BUTTON_BIT(i))
    {
      ret++;
    }
  }

  return ret;
}

uint32_t buttonGetPressedTime(uint8_t ch)
{
  if (ch >= BUTTON_MAX_CH)
  {
    return 0;
  }

  return button_shared.press_time[ch];
}

uint32_t buttonGetReleasedTime(uint8_t ch)
{
  if (ch >= BUTTON_MAX_CH)
  {
    return 0;
  }

  return button_shared.release_time[ch];
}

uint32_t buttonGetPressCount(uint8_t ch)
{
  if (ch >= BUTTON_MAX_CH)
  {
    return 0;
  }

  return button_shared.press_cnt[ch];
}

uint32_t buttonGetRepeatCount(uint8_t ch)
{
  if (ch >= BUTTON_MAX_CH)
  {
    return 0;
  }

  return button_shared.repeat_cnt[ch];
}

void buttonSetRepeatTime(uint8_t ch, uint32_t detect_ms, uint32_t repeat_delay_ms, uint32_t repeat_ms)
{
  if (ch >= BUTTON_MAX_CH)
  {
    return;
  }

  button_tbl[ch].repeat_time_detect = detect_ms;
  button_tbl[ch].repeat_time_delay  = repeat_delay_ms;
  button_tbl[ch].repeat_time        = repeat_ms;
}

const char *buttonGetName(uint8_t ch)
{
  ch = constrain(ch, 0, BUTTON_MAX_CH-1);

  return button_pin[ch].p_name;
}




//-- 스냅샷 API
//
bool buttonInputInit(button_input_t *p_in)
{
  button_pub_t pub;


  if (p_in == NULL)
  {
    return false;
  }

  memset(p_in, 0, sizeof(button_input_t));

  // 생성 시점의 카운터를 기준으로 잡는다. 그래야 첫 update() 에서
  // 이전에 눌렸던 것들이 엣지로 쏟아지지 않는다.
  //
  buttonCopy(&pub);

  p_in->data = pub.data;
  for (int i=0; i<BUTTON_MAX_CH; i++)
  {
    p_in->press_cnt[i]    = pub.press_cnt[i];
    p_in->release_cnt[i]  = pub.release_cnt[i];
    p_in->repeat_cnt[i]   = pub.repeat_cnt[i];
    p_in->press_time[i]   = pub.press_time[i];
    p_in->release_time[i] = pub.release_time[i];
  }
  p_in->hold_done = p_in->data;               // 이미 눌려있는 버튼은 hold 대상에서 제외

  p_in->is_init = true;

  return true;
}

bool buttonInputUpdate(button_input_t *p_in)
{
  button_pub_t pub;


  if (p_in == NULL || p_in->is_init != true)
  {
    return false;
  }

  buttonCopy(&pub);

  p_in->data     = pub.data;
  p_in->pressed  = 0;
  p_in->released = 0;

  for (int i=0; i<BUTTON_MAX_CH; i++)
  {
    uint16_t cnt;

    // 카운터 delta 로 엣지를 만든다. 갱신 주기가 길어서 그 사이 눌렀다 뗐어도
    // 놓치지 않으며, 오래된 이벤트가 남지도 않는다. (16bit 랩어라운드 안전)
    //
    cnt = pub.press_cnt[i] - p_in->press_cnt[i];
    if (cnt > 0)
    {
      p_in->pressed |= BUTTON_BIT(i);
      p_in->press_cnt[i] = pub.press_cnt[i];
      p_in->hold_done &= ~BUTTON_BIT(i);
    }

    cnt = pub.release_cnt[i] - p_in->release_cnt[i];
    if (cnt > 0)
    {
      p_in->released |= BUTTON_BIT(i);
      p_in->release_cnt[i] = pub.release_cnt[i];
      p_in->hold_done &= ~BUTTON_BIT(i);
    }

    p_in->repeat[i] = pub.repeat_cnt[i] - p_in->repeat_cnt[i];
    p_in->repeat_cnt[i] = pub.repeat_cnt[i];

    p_in->press_time[i]   = pub.press_time[i];
    p_in->release_time[i] = pub.release_time[i];
  }

  return true;
}

bool buttonInputGetPressed(button_input_t *p_in, uint8_t ch)
{
  if (p_in == NULL || ch >= BUTTON_MAX_CH)
  {
    return false;
  }

  return (p_in->pressed & BUTTON_BIT(ch)) ? true : false;
}

bool buttonInputGetReleased(button_input_t *p_in, uint8_t ch)
{
  if (p_in == NULL || ch >= BUTTON_MAX_CH)
  {
    return false;
  }

  return (p_in->released & BUTTON_BIT(ch)) ? true : false;
}

bool buttonInputGetHold(button_input_t *p_in, uint8_t ch, uint32_t hold_ms)
{
  if (p_in == NULL || ch >= BUTTON_MAX_CH)
  {
    return false;
  }

  // 판정 시간이 바뀌면 다시 판정한다.
  //
  if (p_in->hold_ms[ch] != hold_ms)
  {
    p_in->hold_ms[ch] = hold_ms;
    p_in->hold_done &= ~BUTTON_BIT(ch);
  }

  if ((p_in->data & BUTTON_BIT(ch)) == 0)     return false;
  if (p_in->hold_done & BUTTON_BIT(ch))       return false;
  if (p_in->press_time[ch] < hold_ms)         return false;

  p_in->hold_done |= BUTTON_BIT(ch);

  return true;
}

uint32_t buttonInputGetRepeat(button_input_t *p_in, uint8_t ch)
{
  if (p_in == NULL || ch >= BUTTON_MAX_CH)
  {
    return 0;
  }

  return p_in->repeat[ch];
}

bool buttonInputGetLevel(button_input_t *p_in, uint8_t ch)
{
  if (p_in == NULL || ch >= BUTTON_MAX_CH)
  {
    return false;
  }

  return (p_in->data & BUTTON_BIT(ch)) ? true : false;
}

uint32_t buttonInputGetPressedTime(button_input_t *p_in, uint8_t ch)
{
  if (p_in == NULL || ch >= BUTTON_MAX_CH)
  {
    return 0;
  }

  return p_in->press_time[ch];
}

uint32_t buttonInputGetReleasedTime(button_input_t *p_in, uint8_t ch)
{
  if (p_in == NULL || ch >= BUTTON_MAX_CH)
  {
    return 0;
  }

  return p_in->release_time[ch];
}




#if CLI_USE(HW_BUTTON)
void cliButton(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("is_init : %s\n", is_init ? "True":"False");
    for (int i=0; i<BUTTON_MAX_CH; i++)
    {
      cliPrintf("%d %-12s pin %2d : %d, press %d\n",
                i,
                buttonGetName(i),
                (int)button_pin[i].pin,
                buttonGetPressed(i),
                buttonGetPressCount(i));
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "show"))
  {
    while(cliKeepLoop())
    {
      for (int i=0; i<BUTTON_MAX_CH; i++)
      {
        cliPrintf("%d %-12s %d, %5d ms\n", i, buttonGetName(i), buttonGetPressed(i), buttonGetPressedTime(i));
      }
      delay(50);
      cliMoveUp(BUTTON_MAX_CH);
    }
    cliMoveDown(BUTTON_MAX_CH);
    ret = true;
  }

  // 스냅샷 사용 예. CLI 가 자기 버퍼를 쓰므로 앱의 이벤트를 뺏지 않는다.
  //
  if (args->argc == 1 && args->isStr(0, "event"))
  {
    button_input_t btn_in;

    buttonInputInit(&btn_in);

    while(cliKeepLoop())
    {
      buttonInputUpdate(&btn_in);

      for (int i=0; i<BUTTON_MAX_CH; i++)
      {
        uint32_t repeat_cnt;

        if (buttonInputGetPressed(&btn_in, i) == true)
        {
          cliPrintf("%d %-12s pressed\n", i, buttonGetName(i));
        }
        if (buttonInputGetHold(&btn_in, i, 1000) == true)
        {
          cliPrintf("%d %-12s hold 1000ms\n", i, buttonGetName(i));
        }
        if (buttonInputGetReleased(&btn_in, i) == true)
        {
          cliPrintf("%d %-12s released, %d ms (%s)\n",
                    i,
                    buttonGetName(i),
                    buttonInputGetPressedTime(&btn_in, i),
                    buttonInputGetPressedTime(&btn_in, i) < 1000 ? "short":"long");
        }
        repeat_cnt = buttonInputGetRepeat(&btn_in, i);
        if (repeat_cnt > 0)
        {
          cliPrintf("%d %-12s repeat %d\n", i, buttonGetName(i), repeat_cnt);
        }
      }
      delay(10);
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("button info\n");
    cliPrintf("button show\n");
    cliPrintf("button event\n");
  }
}
#endif


#endif
