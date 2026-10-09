# 21. UART + CLI + 로그

> MCU-Link VCOM(LPUART1)으로 부팅 배너, 로그, CLI 를 띄운 기록. 이후 모든 단계의 검증은 이 콘솔로 한다.
> 관련: [03-board-mapping.md](03-board-mapping.md) 5절 · [20-led.md](20-led.md) · [12-project-skeleton.md](12-project-skeleton.md)

---

## 1. 무엇을 가져왔나

titan-mini 의 드라이버를 쓴다. 벤더 의존이 없는 것은 그대로 복사했고, uart 만 NXP 용으로 다시 썼다.

| 파일 | 출처 | 변경 |
|---|---|---|
| `common/hw/include/{uart,cli,log}.h` | titan-mini | 그대로 |
| `hw/driver/cli.c` | titan-mini | 그대로 — 벤더 의존 없음 |
| `hw/driver/log.c` | titan-mini | 그대로 — RTOS 부분은 `_USE_HW_RTOS` 로 막혀 있다 |
| `hw/driver/uart.c` | titan-mini 의 공개 API + 구조 | **FSP SCI 부분을 `fsl_lpuart` 로 교체** |
| `nxp/drivers/fsl_lpuart.[ch]` | SDK `mcuxsdk-core/drivers/lpuart` | fetch 목록에 추가 ([11](11-sdk-vendoring.md)) |

## 2. 하드웨어

| 항목 | 값 |
|---|---|
| UART | LPUART1 |
| TX / RX | `GPIO_AON_08` / `GPIO_AON_09`, ALT0 (`IOMUXC_GPIO_AON_08_LPUART1_TX`) |
| 점퍼 | J43, J41 1-2 (기본) |
| 연결 | 레벨 변환기 U30 → MCU-Link VCOM → J23 USB |
| 호스트 포트 | macOS 는 `/dev/cu.usbmodem<시리얼>3` |
| 설정 | 115200 8N1 |

## 3. uart.c 에서 NXP 에 맞춘 부분

```c
static uart_hw_t uart_hw_tbl[UART_MAX_CH] =
{
  {"LPUART1 MCU-Link", LPUART1, LPUART1_IRQn,
   kCLOCK_Root_Lpuart0102, {.mux = kCLOCK_LPUART0102_ClockRoot_MuxSysPll3Div2, .div = 10},
   {IOMUXC_GPIO_AON_08_LPUART1_TX}, {IOMUXC_GPIO_AON_09_LPUART1_RX}, NULL},
};
```

`uartOpen()` 은 다음 순서로 설정한다.

1. `CLOCK_EnableClock(kCLOCK_Iomuxc2)`. **AON 도메인 핀은 IOMUXC LPCG 가 `Iomuxc2`** 다. LED 의 EMC 핀은 `Iomuxc1` 이었다.
2. 핀먹스 ALT0 을 건다. 패드 설정 `0x02` 는 SDK `BOARD_InitDEBUG_UARTPins()` 와 같다.
3. `CLOCK_SetRootClock(kCLOCK_Root_Lpuart0102, …)` 로 SysPll3Div2(240 MHz) ÷ 10 = **24 MHz** 를 만든다. LPUART1 과 LPUART2 는 이 루트 하나를 같이 쓴다.
4. `LPUART_Init(…, CLOCK_GetRootClockFreq())` 에 실제 주파수를 넘긴다. 상수를 쓰지 않는다.
5. 수신 인터럽트를 켠다(`RxDataRegFull`, `RxOverrun`). `LPUART1_IRQHandler` 가 qbuffer 에 넣는다.

송신은 `LPUART_WriteBlocking()` 폴링이다. 115200 bps 에서 256 B 가 약 22 ms 다. 로그가 많아지면 DMA 로 바꾼다.

### PLL3 은 ROM 이 켜 둔 것을 쓴다

클럭 설정은 아직 하지 않았다(로드맵 22). 그런데 SysPll3Div2 를 고를 수 있는 것은 **ROM 이 FlexSPI 클럭(PLL3_PFD2) 때문에 PLL3 을 켜 두었기 때문**이다. 실제로 `boot info` 의 `Clock LPUART1 : 24000000 Hz` 로 확인했다. 로드맵 22 에서 PLL 을 다시 잡을 때 이 의존을 끊지 않도록 주의한다.

### printf 도 콘솔로 간다

`bsp/syscalls.c` 의 `_write` 는 `weak` 다. `uart.c` 가 같은 이름으로 정의해 덮는다. 그래서 `printf()` 와 SDK `assert` 메시지도 LPUART1 로 나온다.

## 4. hw.c — 부팅 배너와 `boot` 명령

초기화 순서는 titan-mini 와 같다. `cliInit` 과 `logInit` 을 먼저 부른다(버퍼만 잡는다). 그 뒤 `ledInit`, `uartInit`, `uartOpen`, `logOpen`, 배너, `cliOpen` 순이다.

배너는 다음과 같다.

```
[ Firmware Begin... ]
Booting..Name  		: FRDM-IMXRT1186-CM33
Booting..Ver   		: V261009R2
Booting..Clock 		: 200 MHz
Booting..Mode  		: 0x0C000000
```

**`boot info`** 는 이 칩을 배우면서 디버거로 하나씩 읽던 값을 펌웨어가 직접 보여 주는 명령이다. 출력은 [01-boot-sequence.md](01-boot-sequence.md) 의 표와 같아야 한다.

```
cli# boot info
SBMR2        : 0x0C000000
  BOOT_MODE  : 100  FlexSPI NOR
  boot core  : CM33
SRSR (reset) : 0x00010201
GPR9 stage   : 0x0000

BOOT_CFG0    : 0x00000000
BOOT_CFG1    : 0x00000000
BOOT_CFG2    : 0x00000080
…
BOOT_CFG7    : 0x00000000
…
  XSPI_INSTANCE : FlexSPI2
  BOOT_FREQ     : RCOSC200M (200 MHz)

Clock M33     : 200000000 Hz
Clock FlexSPI2: 99310344 Hz
Clock LPUART1 : 24000000 Hz
```

이 출력에서 새로 알게 된 것이 있다. **FlexSPI2 는 99.3 MHz 로 돌고 있다.** ROM 이 우리 FCB 의 `serialClkFreq = 100MHz` 를 적용한 결과다. FCB 가 실제로 부팅에 쓰인다는 증거이기도 하다([10](10-dev-environment.md) 6-3 과 대비된다. probe-rs 의 기록은 FCB 를 쓰지 않는다).

## 5. CLI 명령

| 명령 | 출처 | 내용 |
|---|---|---|
| `help` | cli.c | 명령 목록 |
| `md addr [len]` | cli.c | 메모리 덤프 |
| `log info` / `log boot` / `log list` | log.c | 로그 버퍼 |
| `uart info` / `uart test ch` | uart.c | 속도, 클럭, 송수신 바이트 수 / 에코 |
| `boot info` | hw.c | 부트 모드, 리셋 원인, 퓨즈, 클럭 루트 |

```
cli# uart info
_DEF_UART1 : LPUART1 MCU-Link 115200 bps, clk 24000000 Hz  rx 25, tx 936
```

## 6. 검증

baram-term 이 포트를 잡고 있지 않을 때는 스크립트로 확인한다. Python 표준 라이브러리 `termios` 로 포트를 연다.

1. `probe-rs reset` 으로 POR 을 건다.
2. 1.5 초 동안 받아 배너를 확인한다.
3. `help`, `boot info`, `uart info` 를 보내 응답을 확인한다.

위 4~5절의 출력이 그 결과다. LED 는 그대로 500 ms 마다 토글된다. `ap.c` 는 루프에서 `cliMain()` 을 같이 부른다.

| 항목 | 값 |
|---|---|
| FLASH | 30,136 B (LED 단계 10,908 B → `vsnprintf` 등 newlib 이 대부분) |
| DTCM | 18,336 B (로그 버퍼 2 KB, UART 수신 버퍼 1 KB 포함) |
| 경고 | 0 |
| 계층 검사 | 14개 파일, 위반 0 |

## 7. 걸렸던 것

크게 막힌 곳은 없었다. 아래는 다음에 같은 실수를 하지 않으려고 적어 둔다.

- **IOMUXC LPCG 가 도메인마다 다르다.** AON 핀(`GPIO_AON_xx`)은 `kCLOCK_Iomuxc2`, EMC/AD 핀은 `kCLOCK_Iomuxc1` 이다. SDK `pin_mux.c` 의 함수마다 어느 쪽을 켜는지 보면 된다.
- **클럭 루트 공유.** LPUART1 과 LPUART2 는 `Lpuart0102` 루트를 같이 쓴다. 나중에 LPUART2 를 다른 속도로 쓰려면 분주를 같이 따져야 한다.

## 8. 다음

- [ ] 22 — PLL. CM33 300 MHz. LPUART 가 SysPll3Div2 에 기대고 있으니 PLL3 을 건드릴 때 주의한다
- [ ] 송신 DMA (로그량이 늘면)
- [ ] baram-term `--match N1APBXYNYYR5I` 로 CLI 자동 시험
