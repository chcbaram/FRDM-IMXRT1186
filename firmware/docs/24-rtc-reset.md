# 24. RTC · reset — BBNSM

> 시각, 리셋 원인, 리셋을 넘어 유지되는 플래그를 다룬다. 부트로더(로드맵 40~)가 진입 조건을 판단하는 바탕이다. stm32h563-core 의 rtc/reset 을 옮겼다.
> 관련: [01-boot-sequence.md](01-boot-sequence.md) · [21-uart-cli.md](21-uart-cli.md) · [02-memory-map.md](02-memory-map.md) 5절
>
> 출처: RM 25장 BBNSM (25.3.1 RTC, 25.6.1.4 BBNSM_CTRL, 25.6.1.11 GPR), RM 27.6.1.9 SRC SRSR / UM12450 Table 4 (J10, J11) / SDK `fsl_bbnsm.c` (참고만).

---

## 1. 무엇이 어디에 있나

STM32 에서는 RTC 와 백업 레지스터가 RTC/TAMP 블록에 있었다. 이 칩에서는 **BBNSM**(Battery-Backed Non-Secure Module)이 그 역할을 한다.

| 기능 | 레지스터 | 비고 |
|---|---|---|
| 실시간 카운터 | `RTC_MS[14:0] : RTC_LS[31:0]` | 47 bit, **32.768 kHz 틱**. 초 = `MS<<17 \| LS>>15` |
| 알람 | `TA` | 카운터 상위 32 bit(= 초)와 비교 |
| **범용 레지스터** | `GPR[0..7]` | 32 bit × 8. **BBSM 도메인이라 시스템 리셋과 POR_B 에도 남는다** |
| 리셋 원인 | SRC `SRSR` | BBNSM 이 아니라 SRC 에 있다 |

전원과 배터리 상황은 다음과 같다.

| 항목 | 상태 | 결과 |
|---|---|---|
| VDD_BBSM | 3V3 에서 **J10** (기본 단락) | 보드 전원이 있는 동안 유지 |
| 코인 배터리 | **J11 DNP** | 보드 전원을 끄면 시각과 GPR 이 지워진다 |

리셋 원인과 플래그를 보관하는 용도로는 충분하다. 전원을 꺼도 시각이 남아야 하면 J11 에 배터리를 단다.

## 2. ROM 이 남긴 BBNSM

프로브로 읽은 값이다.

| 레지스터 | 값 | 의미 |
|---|---|---|
| `BBNSM_CTRL` | `0x0100_0005` | **DP_EN=1 (Dumb PMIC)**, TA_EN=01 (꺼짐), **RTC_EN=01 (꺼짐)** |
| `RTC_MS/LS` | 0 | 멈춰 있음 |
| `GPR0~7` | 0 | |

`RTC_EN` 만 켜도(`0x0100_0006`) 카운터가 1.1 초에 약 36 000 씩 오른다. **32 kHz 클럭(Y1)은 따로 설정할 필요가 없다.**

### ⚠️ TOSP — CTRL 을 통째로 쓰면 전원이 꺼진다

`BBNSM_CTRL[25] TOSP` 는 *"Turn Off System Power"* 다. 이 보드는 DP_EN=1 로 부팅하므로, TOSP 를 세우면 PMIC 로 전원 차단 신호가 나간다. 그래서 다음을 지킨다.

- CTRL 은 **필요한 필드만 읽고-고쳐-쓴다.** `rtcSetEnable()` 은 RTC_EN 만 바꾸고 TOSP 는 항상 0 으로 내보낸다.
- SDK `BBNSM_RTC_Init()` 은 쓰지 않는다. 보정을 켜면 `BBNSM_CTRL = CAL_VAL(...)` 로 레지스터를 통째로 덮어써서 DP_EN 이 지워진다.

## 3. rtc.c

레지스터 몇 개라 SDK 드라이버 없이 직접 썼다.

| 함수 | 구현 |
|---|---|
| `rtcInit()` | RTC_EN 이 꺼져 있으면 켠다 |
| 초 읽기 | MS/LS 를 따로 읽으므로 **같은 값이 두 번 나올 때까지** 반복 (SDK 와 같은 방식) |
| 초 쓰기 | RTC_EN 끄기 → MS/LS 쓰기 → 다시 켜기 |
| 시각 표현 | 카운터에 **유닉스 시간(UTC)** 을 넣고 newlib `gmtime_r` / `mktime` 로 변환 |
| `rtcIsTimeSet()` | 2024-01-01 이후면 맞춘 것으로 본다 |
| `rtcSetReg` / `rtcGetReg` | `GPR[index]`, 0~7 |

`gmtime_r` 과 `mktime` 때문에 FLASH 가 약 9.5 KB 늘었다.

## 4. reset.c

stm32h563-core 의 reset.c 를 그대로 옮기고 리셋 원인 판정만 바꿨다.

### SRSR → 리셋 원인 분류

| 분류 | SRSR 비트 |
|---|---|
| POWER | `POR_RST` (bit 0) |
| PIN | `IPP_POR_B` (bit 16) — SW2 (J24) |
| SOFT | `CM33_REQUEST` (9), `CM7_REQUEST` (11), `JTAG_SW_RST` (8) |
| WDG | `WDOG1~5` (1~5) |
| ETC | lockup (10, 12), EdgeLock (7), 온도센서 (6), DCDC 과전압 (13), ECAT (14) |

**SRSR 은 POR 에서만 지워지고 나머지 리셋에서는 비트가 쌓인다.** 처음 `boot info` 에서 본 `0x0001_0201` 이 쌓인 상태였다. `resetInit()` 은 읽은 값을 그대로 다시 써서 지운다(W1C).

### GPR 배치 (`hw_def.h`)

| GPR | 이름 | 내용 |
|---|---|---|
| 0 | `HW_RTC_BOOT_MODE` | `resetToBoot()` / `resetToUpdate()` 가 남기는 플래그. 다음 부팅에서 읽고 0 으로 지운다 |
| 1 | `HW_RTC_RESET_BITS` | 리셋 원인 비트 |
| 2 | `HW_RTC_RESET_CNT` | 리셋 버튼 더블클릭 카운터 (`0xA55A` 매직 + 값) |
| 3 | `HW_RTC_BOOT_TRY` | 새 펌웨어 부팅 확인 카운터 |
| 4 | `HW_RTC_FAULT_CNT` | 폴트 리셋 횟수 |
| 5 · 6 | `HW_RTC_ECC_ADDR` · `ECC_VALID` | ECC 오류 주소. STM32 판은 매직을 주소에 섞었지만 여기서는 32 bit 주소를 그대로 두고 유효 표시를 따로 둔다 |
| 7 | — | 비어 있음 |

`HW_RESET_BOOT 1` 이라 앱이 직접 더블클릭을 판정한다. stm32h563-core 에서는 부트로더만 판정하고 앱은 0 이다. 로드맵 41 에서 부트로더가 생기면 앱을 0 으로 바꾼다.

## 5. CLI

| 명령 | 내용 |
|---|---|
| `rtc info` | CTRL, 카운터 원값, 초, 날짜, GPR0~7 |
| `rtc set date y m d` / `rtc set time h m s` | UTC 로 설정 |
| `rtc reg idx [data]` | GPR 읽기/쓰기 |
| `reset info` | SRSR 원본, 리셋 원인, 부트 모드, 카운터들 |
| `reset boot` / `reset update` / `reset reset` | 플래그를 남기고 소프트 리셋 |
| `reset fault inc\|clear` | 폴트 카운터 시험 |

## 6. 검증

| 시험 | 결과 |
|---|---|
| `rtcInit` 후 | `CTRL = 0x0100_0006` — RTC_EN 만 바뀌고 DP_EN 은 그대로 |
| 시각 설정 → AIRCR 리셋 → 다시 읽기 | 2026-10-09 14:08:14 UTC 로 설정한 시각이 리셋 뒤에도 흐른다 |
| `rtc reg 7 0x12345678` → AIRCR 리셋 | `GPR7 = 0x12345678` 유지 |
| AIRCR SYSRESETREQ | SRSR `0x0000_0200` (CM33_REQUEST) → `RESET_BIT_SOFT` |
| **`probe-rs reset` (기록 직후 리셋)** | SRSR `0x0000_0200` → **SOFT**. 핀 리셋이 아니다 |
| FLASH / DTCM | 44,360 B / 18,792 B, 경고 0, 계층 검사 19개 파일 위반 0 |

### `probe-rs reset` 은 핀 리셋이 아니었다

[22-clock.md](22-clock.md) 5절에서는 *"MCU-Link 가 J26 로 POR_B 를 건다"* 고 추정했다. SRSR 을 읽어 보니 `CM33_REQUEST` 만 남았다. **probe-rs 의 리셋은 SWD 로 SYSRESETREQ 를 쓰는 소프트 리셋**이다. J26(MCU-Link TRG_RST → POR)은 이 경로에 쓰이지 않는다. 다만 `probe-rs reset` 뒤에 VCOM 출력이 사라지는 현상 자체는 여전하다. 원인 추정만 바뀐 것이다.

### 확인 필요 — 손으로 눌러야 하는 시험

- [ ] SW2 한 번 → SRSR 에 어떤 비트가 남는지 (`IPP_POR_B` 만인지, `POR_RST` 도 같이인지). PIN 으로 분류되고 빨간 LED 가 0.3 초 켜지는지
- [ ] SW2 빠르게 두 번 → `reset_count : 2`
- [ ] 전원 재인가 → POWER 로 분류되고 카운터가 0. GPR 과 시각이 지워지는지 (J11 DNP)
- [ ] SW3 (POR_B 직접) 와 SW2 의 차이

## 7. 다음

- [ ] 25 — CM7 기동 (+ 캐시/MPU/TRDC)
- [ ] 폴트 핸들러(fault.c)를 옮겨 `resetIncFaultCount()` 와 ECC 주소 기록을 연결 (stm32h563-core `25-fault`)
