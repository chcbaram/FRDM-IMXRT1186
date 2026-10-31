# 29. event · 버튼 스레드

> 모듈끼리 서로를 부르지 않고 알리는 event(발행 · 구독)를 들이고, 버튼을 원본처럼 1 ms 스레드로 되돌렸다. module 은 28 에서 먼저 들였다([28](28-freertos.md)).
> 관련: [28-freertos.md](28-freertos.md) · [23-button-swtimer.md](23-button-swtimer.md)
>
> 출처: titan-mini `common/hw/include/event.h` · `cpu/cm85/hw/driver/event.c` · `ap/ap.c`, NUCLEO-C5A3ZG `stm32c5-ai/src/hw/driver/button.c`.

---

## 1. 결과

```
[  ] Thread Started : cli
[  ] Event EVENT_BOOT_DONE : 0
cli# event info
sub   : 0 / 16
queue : 0 / 16
pub   : 1
drop  : 0
log   : on
cli# event pub 7 3
pub code 7 data 3
[  ] Event cli : 3
cli# thread info
thread cnt : 2 / 8
name             pri  stack  free(word)
button           4    1024   228
cli              3    4096   856
```

## 2. event — titan-mini 그대로

```
발행 (아무 스레드)              eventPub(EVENT_BUTTON, ch)  → 큐 (16)
main 스레드 루프                eventUpdate()               → 구독자마다 func(&evt)
구독                            모듈의 event_cb (module.c 가 init 성공 뒤 자동 등록) 또는 eventSub(func)
```

- 발행하는 쪽은 무엇이 받는지 모르고, 받는 쪽은 누가 보냈는지 모른다. 코드는 `common/evt_code.h` (titan-mini 와 같다: `EVENT_BOOT_DONE`, `EVENT_ERROR`, `EVENT_ETH_LINK`, `EVENT_ETH_IP`, `EVENT_USB_CONNECT`, `EVENT_SD_DETECT`, `EVENT_BUTTON`).
- `eventPub()` 은 매크로라 코드 이름이 문자열로 같이 간다. 로그에 숫자 대신 이름이 찍힌다.
- 큐가 넘치면 버리지 않고 센다(`event info` 의 drop).
- **바꾼 것 하나:** 원본은 `eventPub()` 에 보호가 없다. 여기서는 cli · button · main 이 모두 발행하므로 큐에 넣는 곳을 `taskENTER_CRITICAL()` 로 감쌌다. ISR 에서는 부르지 않는다.
- `module.h` 의 `event_cb` 가 `_USE_HW_EVENT` 로 살아났다. 모듈이 `.event_cb = ...` 만 적으면 init 이 성공한 뒤 구독자로 올라간다.

| CLI | |
|---|---|
| `event info` | 구독자 수 · 큐 · 발행 수 · 버린 수 · 로그 |
| `event log on/off` | `eventUpdate()` 가 이벤트마다 찍는 로그 |
| `event pub code data` | 손으로 발행 — 구독자 동작 확인용 |

### apMain — titan-mini 흐름

```c
threadBegin();                  // 등록된 스레드 생성 (cli, button)
logBoot(false);                 // 부팅 로그는 여기까지 (log boot 로 다시 본다)
eventPub(EVENT_BOOT_DONE, 0);
while (1)
{
  LED 500 ms
  eventUpdate();
  moduleUpdate();
  delay(1);
}
```

## 3. 버튼 — 1 ms 스레드로 되돌림

로드맵 23 에서는 RTOS 가 없어 SysTick(그 뒤엔 FreeRTOS 틱 훅)의 swtimer 로 1 ms 마다 갱신했다. 원본(NUCLEO-C5A3ZG `stm32c5-ai`)처럼 스레드로 되돌렸다.

| | 전 (23 · 28) | 후 |
|---|---|---|
| 갱신 | swtimer → ISR 문맥 | `button` 스레드 (pri 4, 1 KB), `vTaskDelayUntil` 1 ms |
| 공유본 보호 | reader 만 인터럽트 막고 복사 | 양쪽 `taskENTER_CRITICAL()` (원본과 같다) |
| 알림 | 없음 | 눌림 순간 `eventPub(EVENT_BUTTON, ch)` |

- 스레드는 `buttonInit()` 이 **등록만** 하고 `apMain()` 의 `threadBegin()` 에서 만들어진다.
- 버튼 스레드 우선순위(4)가 cli(3) · main(2) 보다 높다. 1 ms 주기를 지키기 위해서다.
- swtimer 와 틱 훅은 그대로 남겼다. 지금은 쓰는 곳이 없다.

## 4. 확인 필요

- [ ] **SW4 를 실제로 눌러 `[  ] Event EVENT_BUTTON : 0` 이 찍히는지.** 2 분 동안 콘솔을 기록했지만 그동안 누르지 않아 확인하지 못했다. `button info` 의 `press` 카운트도 같이 본다. (23 의 수동 확인 항목과 같다)

## 5. 다음

- [ ] 구독자 첫 사용 — 예: `EVENT_BUTTON` 을 받아 LED 를 바꾸는 작은 모듈, 또는 50 번대의 `EVENT_ETH_LINK`
- [ ] CM7 에도 event (필요해지면). CM7 은 `module` · `thread` 까지 들였다([28](28-freertos.md) 5절)
- [ ] 코어 사이 이벤트 — CM7 의 이벤트를 cmd 패킷으로 CM33 에 넘길지 (예: CM7 네트워크 링크 → CM33 LED)
