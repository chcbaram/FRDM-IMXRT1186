# 23. 버튼(SW4) · swtimer

> SysTick 1 ms 에서 도는 소프트웨어 타이머와, 그 위에서 갱신되는 사용자 버튼 드라이버. RTOS 가 들어오기 전까지 주기 작업은 swtimer 로 돌린다.
> 관련: [03-board-mapping.md](03-board-mapping.md) 4절 · [21-uart-cli.md](21-uart-cli.md) · [22-clock.md](22-clock.md)

---

## 1. 가져온 것

titan-mini 에는 버튼과 swtimer 가 없다. 그래서 **NUCLEO-C5A3ZG** 에서 가져왔다. N657X0 이 계층 구조를 가져온 원본 프로젝트다.

| 파일 | 출처 | 변경 |
|---|---|---|
| `common/hw/include/swtimer.h`, `common/hw/src/swtimer.c` | C5A3ZG `stm32c5-fw` | 그대로 — 포터블 |
| `common/hw/include/button.h` | C5A3ZG `stm32c5-ai` | 그대로 |
| `hw/driver/button.c` | C5A3ZG `stm32c5-ai` | GPIO 접근을 RGPIO 로, **RTOS 스레드를 swtimer 콜백으로** |

`hw_def.h` 의 배치는 최근 프로젝트(nu54v-dk, stm32h5-bd)를 따른다. 핀 이름 enum `ButtonPinName_t` 는 **맨 아래**에 두고, 채널 수는 `HW_BUTTON_MAX_CH BUTTON_PIN_MAX` 로 enum 에서 가져온다.

## 2. 하드웨어 — SW4

회로도 4페이지에 따르면 다음과 같다.

| 항목 | 값 |
|---|---|
| 핀 | `GPIO_AD_12` → **RGPIO4.12**, ALT5 (`IOMUXC_GPIO_AD_12_GPIO4_IO12`) |
| 점퍼 | J30 1-2 (기본) |
| 동작 | 누르면 **GND** 로 연결. C98 0.1 µF 로 RC 필터 |
| 풀업 | 외부 R279 100K 는 **DNP** → **내부 풀업** 필요 |
| 극성 | **active low** |
| 패드 설정 | `PUE | PUS = 0x0C` |

같은 페이지에서 LED 가 NPN 트랜지스터(BC817, Q5/Q6)로 구동되는 것도 확인했다. [20-led.md](20-led.md) 의 active high 와 맞는다.

## 3. swtimer

`SysTick_Handler` 가 1 ms 마다 `swtimerISR()` 을 부른다(`bsp/bsp.c`). swtimer 는 등록된 타이머의 카운트를 줄인다. 카운트가 0 이 되면 콜백을 부르고, `LOOP_TIME` 이면 다시 장전한다. 슬롯은 `HW_SWTIMER_MAX_CH 8` 개다.

```c
swtimer_handle_t h = swtimerGetHandle();
swtimerSet(h, 1, LOOP_TIME, buttonISR, NULL);   // 1 ms 마다
swtimerStart(h);
```

콜백은 **인터럽트 문맥**에서 돈다. 짧게 끝나야 하고, 블로킹 출력(`logPrintf` 등)을 하면 안 된다.

## 4. button — RTOS 없이 돌리기

원본은 1 ms 주기의 버튼 스레드가 다음 일을 한다.

- `buttonUpdate()`: 디바운스 10 ms, 눌림/뗌 판정, 오토 리피트
- `buttonPublish()`: 공유본으로 복사

여기서는 스레드 대신 swtimer 콜백 `buttonISR()` 이 같은 일을 한다.

| | 원본 (RTOS) | 여기 |
|---|---|---|
| 갱신 | `buttonThread` + `osDelayUntil(1)` | swtimer 1 ms 콜백 (SysTick 인터럽트) |
| 발행 `buttonPublish` | `taskENTER_CRITICAL` 안에서 memcpy | 그냥 memcpy — writer 는 인터럽트라 reader 에게 선점되지 않는다 |
| 복사 `buttonCopy` | `taskENTER_CRITICAL` | `__disable_irq` + PRIMASK 복원 |

로드맵 28(FreeRTOS)에서 스레드로 되돌린다.

### 스냅샷 API

드라이버는 **상태와 누적 카운터만** 발행한다. 눌림 비트맵, 누적 눌림/뗌/리피트 횟수, 시간이다. 엣지는 각 소비자가 자기 `button_input_t` 사본의 지난 카운터와 비교해 만든다.

- 소비자끼리 이벤트를 뺏지 않는다.
- 오래된 이벤트가 남지 않는다.
- 같은 루프 안에서는 엣지와 시간이 같은 시점의 값이다.

```c
static button_input_t button_input;                 // 소비자마다 하나

buttonInputUpdate(&button_input);                   // 루프마다 한 번
if (buttonInputGetPressed(&button_input, _DEF_BUTTON1))     ledToggle(_DEF_LED3);   // 눌림 엣지 → 파랑
if (buttonInputGetHold(&button_input, _DEF_BUTTON1, 1000))  ledToggle(_DEF_LED1);   // 1 초 롱프레스 → 빨강
```

## 5. CLI

| 명령 | 내용 |
|---|---|
| `button info` | 채널, 핀, 현재 상태, 누적 눌림 횟수 |
| `button show` | 상태와 눌림 시간을 계속 표시 |
| `button event` | 눌림 / 홀드 / 뗌 / 리피트 이벤트를 계속 표시 |

## 6. 검증

| 확인 | 결과 |
|---|---|
| 초기화 로그 | `[OK] buttonInit()` |
| `button info` (누르지 않음) | `0 BTN_USER     pin 12 : 0, press 0` |
| `IOMUXC MUX GPIO_AD_12` (`0x42A1_013C`) | `0x5` — ALT5 |
| `IOMUXC PAD GPIO_AD_12` (`0x42A1_0384`) | `0x0C` — 내부 풀업 |
| `RGPIO4 PDIR` (`0x5383_0050`) | `0xE00F5000` — **bit 12 = 1** (풀업으로 High) |
| `RGPIO4 PDDR` | 0 — 입력 |
| 녹색 LED 500 ms · UART · CLI | 그대로 동작 |
| FLASH / DTCM | 34,888 B / 18,624 B, 경고 0 |

### 확인 필요

- [ ] **SW4 를 실제로 눌렀을 때** `PDIR` bit 12 가 0 이 되는지, 파란 LED(눌림)와 빨간 LED(1 초 홀드)가 토글되는지. 30 초 모니터를 돌렸지만 그동안 누르지 않아 아직 확인하지 못했다.

## 7. 다음

- [ ] 24 — RTC · reset (BBNSM GPR)
- [ ] 25 — CM7 기동 (+ 캐시/MPU/TRDC)
