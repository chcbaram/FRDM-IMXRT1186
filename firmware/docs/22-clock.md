# 22. 클럭 — CM33 240 MHz

> ROM 이 넘겨준 200 MHz(RC 발진기)에서 PLL 기반 240 MHz 로 옮긴 기록. PLL 은 다시 설정하지 않고 클럭 루트만 바꿨다. 바꾸기 전과 후를 CCM 의 하드웨어 측정 기능으로 비교했다.
> 관련: [01-boot-sequence.md](01-boot-sequence.md) 3절 · [21-uart-cli.md](21-uart-cli.md)
>
> 출처: IMXRT1180EC 데이터시트 (동작 모드별 주파수), RM 12.4.4 Clock Configuration for Boot ROM, SDK `frdmimxrt1186/common/clock/clock_config.c` (`BOARD_BootClockRUN`), `fsl_clock.h` (`CCM_OBS_*`).

![클럭 트리](images/clock-tree.svg)

---

## 1. 목표를 300 MHz 에서 240 MHz 로 바꿨다

로드맵에는 "CM33 300 MHz" 라고 적었다. 데이터시트를 확인하니 조건이 붙어 있었다.

| 동작 모드 (IMXRT1180EC) | CM33 | 전압 |
|---|---|---|
| **Normal Drive Run** | **240 MHz** | 1.0 V |
| Over Drive Run | 300 MHz | 1.1 V |
| Under Drive Run | 100 MHz | 낮춤 |

300 MHz 는 오버드라이브 전압이 필요하다. 클럭원도 문제다. CM33 루트의 mux 후보는 RC24M, RC400M, SYS_PLL3, ARM_PLL 뿐이다. SYS_PLL3(480 MHz)으로는 300 MHz 를 정수 분주로 만들 수 없다. ARM_PLL 은 CM7 800 MHz 에 써야 한다.

그래서 **SDK `BOARD_BootClockRUN()` 과 같은 240 MHz(SYS_PLL3 ÷ 2, Normal Drive)** 로 정했다.

## 2. ROM 이 남긴 클럭 — 먼저 측정했다

`clock info` 명령을 먼저 만들고, 클럭을 바꾸지 않은 상태(`CLOCK_APPLY 0`)로 읽었다. **calc** 는 CCM 레지스터로 계산한 값이다. **meas** 는 CCM OBSERVE 가 하드웨어로 센 값이다(`CLOCK_GetFreqFromObs`).

```
OSC 24M      : on (stable)
ARM_PLL    : off                  0 Hz
SYS_PLL1   : off                  0 Hz
SYS_PLL2   : on           528000000 Hz
SYS_PLL3   : on           480000000 Hz

root         mux div      calc Hz      meas Hz
M33        :   1   2    200000000    199738624
EDGELOCK   :   1   3    133333333    133122048
BUS_AON    :   1   4    100000000     99873792
BUS_WAKEUP :   1   4    100000000     99872256
WAKEUP_AXI :   1   3    133333333    133180160
FLEXSPI2   :   2   3     99310344     99317504
LPUART0102 :   2  10     24000000     24001536
```

여기서 알게 된 것이 세 가지다.

- RM 이 *"RCOSC200M"* 이라 부르는 것은 실제로는 **OSC_RC_400M ÷ 2** 다(mux 1, div 2).
- RC 발진기라 실측이 **0.13 % 낮다**(199.74 MHz). 이 상태로 SysTick 을 쓰면 시간도 그만큼 늦게 간다.
- **SYS_PLL2 와 SYS_PLL3 는 ROM 이 이미 켜 두었다.** 24 MHz 크리스탈도 켜져 있다. ARM_PLL 과 SYS_PLL1 은 꺼져 있다.

## 3. 무엇을 바꿨나 — `bsp/clock.c`

| 루트 | ROM | 바꾼 뒤 | 근거 |
|---|---|---|---|
| M33 | RC400M ÷ 2 = 200 MHz | **SYS_PLL3 ÷ 2 = 240 MHz** | SDK 와 같음 |
| BUS_AON | RC400M ÷ 4 = 100 MHz | **SYS_PLL2 ÷ 4 = 132 MHz** | 〃 |
| BUS_WAKEUP | RC400M ÷ 4 = 100 MHz | **SYS_PLL2 ÷ 4 = 132 MHz** | 〃 |
| WAKEUP_AXI | RC400M ÷ 3 = 133 MHz | **SYS_PLL3 ÷ 2 = 240 MHz** | 〃 |
| FLEXSPI2 | PLL3_PFD2 ÷ 3 = 99.3 MHz | 그대로 | XIP 중 |
| EDGELOCK | RC400M ÷ 3 = 133 MHz | 그대로 | ELE 클럭은 ELE API 로 바꿔야 한다(SDK `EdgeLock_SetClock`) |

순서는 다음과 같다.

1. 24 MHz 크리스탈이 안정 상태인지 본다. 꺼져 있으면 켠다. 이미 켜져 있으면 손대지 않는다.
2. SYS_PLL2 와 SYS_PLL3 가 켜져 있고 bypass 가 아닌지 본다. **아니면 아무것도 바꾸지 않고 ROM 클럭으로 남는다.**
3. 버스 루트를 먼저 바꾸고, 마지막에 M33 을 바꾼다. CCM 클럭 루트의 mux/div 변경은 하드웨어가 글리치 없이 처리한다.
4. `SystemCoreClock` 을 CCM 에서 다시 읽는다. 그다음에 `bspInit()` 이 `SysTick_Config()` 를 한다.

### 왜 PLL 을 다시 설정하지 않나

SDK 는 PLL 을 전부 처음부터 다시 잡는다. 그 전에 `BOARD_FlexspiClockSafeConfig()` 로 FlexSPI 를 안전한 클럭으로 옮기고, FlexSPI 클럭을 바꾸는 함수(`BOARD_SetFlexspiClock`)는 **RAM 에서 실행**한다. 지금 도는 코드가 FlexSPI2 에서 XIP 로 페치되는데, FlexSPI2 클럭이 PLL3_PFD2 에서 오기 때문이다.

우리는 ROM 이 PLL2 와 PLL3 을 이미 켜 두었다는 것을 측정으로 확인했다. 그래서 **루트만 옮기면 같은 결과가 나오고, XIP 클럭원은 건드리지 않는다.** PLL 을 직접 잡아야 하는 것은 ARM_PLL(CM7 800 MHz)과 SYS_PLL1(NETC/ENET)이다. 이 둘은 각각 로드맵 25 와 50 에서 다룬다.

> RM 12.4.4 의 경고 *"PLL 이나 PFD 를 다시 설정할 때는 먼저 코어·버스 루트를 고정 클럭으로 옮겨라"* 는 PLL 을 바꿀 때 해당한다. 이번에는 PLL 을 바꾸지 않았다.

## 4. 결과

```
[ Firmware Begin... ]
Booting..Clock 		: 240 MHz
Booting..Clock Init	: PLL

root         mux div      calc Hz      meas Hz
M33        :   2   2    240000000    240016640
EDGELOCK   :   1   3    133333333    133245952
BUS_AON    :   2   4    132000000    132008704
BUS_WAKEUP :   2   4    132000000    132009216
WAKEUP_AXI :   2   2    240000000    240017408
FLEXSPI2   :   2   3     99310344     99316992
LPUART0102 :   2  10     24000000     24001792
```

| 확인 | 결과 |
|---|---|
| M33 실측 | 240.017 MHz. 크리스탈 기준 PLL 이라 계산값과 +0.007 % 차이 |
| 1 ms 틱 | `systick_ms` 를 10 초 간격으로 프로브로 읽음: 10081 증가 / 10088 ms. 차이는 프로브 지연 |
| LED · UART · CLI | 그대로 동작 |
| FLASH | 31,856 B |
| 경고 | 0 (`CLOCK_APPLY 0` 으로 빌드해도 0) |

`CLOCK_APPLY` 를 0 으로 바꾸면 ROM 클럭 그대로 동작한다. 클럭 때문인지 의심되는 문제를 가를 때 쓴다.

## 5. 걸렸던 것

### `probe-rs reset` 으로 리셋하면 부팅 배너를 놓친다

클럭을 바꾼 뒤 배너가 안 보였다. 처음에는 클럭 탓으로 의심했다. 그런데 `CLOCK_APPLY 0` 으로 되돌려도 마찬가지였다.

| 리셋 방법 | 리셋 시간 | 배너 |
|---|---|---|
| `probe-rs reset` (SWD 로 SYSRESETREQ — [24](24-rtc-reset.md) 6절에서 SRSR 로 확인) | 0.13 s | **0 바이트** |
| `probe-rs write … 0xE000ED0C 0x05FA0004` (AIRCR SYSRESETREQ) | 0.06 s | 19 ms 뒤 수신 |

펌웨어의 `uart info` 송신 카운터에는 배너가 잡혀 있었다. 즉 **펌웨어는 보냈고, probe-rs 가 리셋 명령을 처리하는 동안 MCU-Link 가 VCOM 데이터를 버린다.** (처음에는 J26 로 POR_B 를 건다고 추정했으나, 24 에서 SRSR 을 읽어 보니 소프트 리셋이었다) 이후 시리얼 검증 스크립트는 AIRCR 로 리셋한다. SYSRESETREQ 는 POR 이 아니므로 BOOT_MODE 를 다시 샘플하지 않는다. 그러나 ROM 부터 다시 부팅하는 것은 같다.

### VCOM 에서 가끔 앞쪽 몇 바이트가 빠진다 (미해결)

배너와 명령 에코를 반복해 받아 보면, **출력 덩어리 앞쪽 0~8 바이트가 가끔 빠진다.**

| 시험 | ROM 클럭 | PLL 클럭 |
|---|---|---|
| 배너 4회 (원래 191 / 183 B) | 190, 191, 189, 191 | 182, 180, 175, 183 |
| `uart info` 에코 20회 × 3 | 18 / 17 / 20 회 정상 | 18 / 20 회 정상 |

- 클럭과 상관없이 생긴다.
- MCU 수신은 정상이다. 명령은 매번 실행된다.
- 빠지는 것은 MCU → 호스트 방향이고, 송신이 쉬었다가 다시 시작할 때 생긴다.

경로는 LPUART1 TX → 레벨 변환기 U30 → MCU-Link(LPC55S69) VCOM → USB → macOS 다. 로직 분석기로 TX 핀을 보기 전에는 어느 구간인지 가를 수 없다. 다음 후보를 남긴다.

- [ ] TX 핀을 로직 분석기로 본다. MCU 가 보낸 것이 맞으면 MCU-Link 쪽이다.
- [ ] baram-term 같은 다른 터미널에서도 같은지 본다. 시험 스크립트의 문제일 수 있다.
- [ ] MCU-Link 펌웨어 버전(CMSIS-DAP V3.156)을 확인하고 업데이트를 검토한다.
- [ ] LPUART2(VCOM 2)로 같은 시험을 한다.

## 6. 다음

- [x] 23 — 버튼(SW4) · swtimer ([23-button-swtimer.md](23-button-swtimer.md))
- [ ] 25 — ARM_PLL 800 MHz + 오버드라이브(DCDC 1.1 V, FBB) + CM7 기동
