# 03. FRDM-IMXRT1186 보드 결선

> 부팅과 브링업에 필요한 보드 자원 정리. 다룬 범위는 부트 스위치, 리셋, 디버거, LED, 버튼, UART, 메모리, 전원이다.
> 관련: [01-boot-sequence.md](01-boot-sequence.md) · [05-network-overview.md](05-network-overview.md)
>
> 출처: `hardware/SPF-95302_a4.pdf` (회로도 SCH-95302 Rev C), `hardware/UM12450.pdf` (Board User Manual Rev.3.0) Table 4 Jumpers / Table 6 DIP switch / Table 7 LEDs / Table 12 FlexSPI / Table 17 LPUART, SDK `frdmimxrt1186/board.h`.

![보드 블록도](images/board-block.svg)

---

## 1. 칩

| 항목 | 값 |
|---|---|
| MCU | MIMXRT1186CVJ8C (LFBGA196) |
| CM33 | 240 MHz (Normal Drive, [22-clock.md](22-clock.md)) · 300 MHz 는 오버드라이브 · 부팅 직후 200 MHz |
| CM7 | 최대 800 MHz |
| 크리스탈 | Y2 24 MHz (메인), Y1 32.768 kHz (RTC) |

## 2. 부트 스위치와 리셋

![부트 설정](images/board-boot-config.svg)

**J60** 은 3핀 DIP 스위치다. `BOOT_MODE[2:0] = J60[3:1]` 이고, ON 이 1 이다. UM 의 그림 라벨은 SW5 로 되어 있다.

| 쓰임 | J60 [3:1] | 설명 |
|---|---|---|
| **평소 (QSPI 부팅)** | `100` | FlexSPI NOR. 공장 데모도 이 설정 |
| 디버거로 RAM 실행 | `110` | Infinite Loop. ROM 이 아무것도 로드하지 않고 멈춘다 |
| 벽돌 복구 | `001` | 시리얼 다운로더 (USB1 / LPUART1) |

| 버튼 / 점퍼 | 기본 | 동작 |
|---|---|---|
| **SW2** | — | 시스템 리셋. J24 가 단락되어 있으면 **MCU POR** 를 건다 |
| SW3 | — | POR_B 핀 리셋 |
| SW1 | — | ON/OFF. BBSM 저전력 모드 진입/해제 |
| SW6 | — | wake-up |
| **J24** | 단락 | SW2 → POR 연결 |
| **J26** | 단락 | MCU-Link 리셋(TRG_RST) → POR 연결 |
| J9 | 개방 | EWM_OUT_B(워치독) → 리셋 연결 안 함 |

BOOT_MODE 는 POR 때만 샘플된다. **스위치를 바꾼 뒤 SW2 를 누르면 된다.**

## 3. 디버거 (MCU-Link OB)

보드에 MCU-Link(LPC55S69, U20)가 실장되어 있고 **CMSIS-DAP 펌웨어**로 출하된다. USB-C **J23** 하나로 SWD, VCOM, 전원이 모두 들어온다.

| 점퍼 | 기본 | 의미 |
|---|---|---|
| J36 | 개방 | MCU-Link SWD 사용 (단락하면 외부 디버거용으로 끊김) |
| J58 | 개방 | 온보드 RT1186 을 디버깅 (단락하면 외부 타깃) |
| J40 | 개방 | MCU-Link 정상 부팅 (단락하면 LPC55S69 ISP) |
| J50 | 개방 | VCOM 사용 |

외부 디버거는 J62(2x5 SWD)에 꽂고 J36 을 단락한다.

## 4. 사용자 LED / 버튼

D4 는 RGB LED 하나다. 세 신호 모두 **3핀 0402 점퍼 두 개를 거쳐** MCU 에 닿는다(회로도 7페이지). 기본 실장 상태에서 LED 에 연결되어 있다.

| 신호 | MCU 핀 | GPIO | 경로 | 극성 |
|---|---|---|---|---|
| LED_RED_CTL | `GPIO_EMC_B1_09` | **RGPIO2.09** | R210 → R197 | High = ON |
| LED_GREEN_CTL | `GPIO_EMC_B1_11` | **RGPIO2.11** | R368 → R379 | High = ON |
| LED_BLUE_CTL | `GPIO_EMC_B1_39` | **RGPIO3.07** | R84 → R83 | High = ON |
| SW4 (GPIO_BUTTON) | `GPIO_AD_12` | **RGPIO4.12** | J30 1-2 (기본) | 확인 필요 |

- 극성은 SDK `board.h` 의 `LOGIC_LED_ON (1U)` 와 `BOARD_USER_LED_GPIO RGPIO2 / PIN 11` 에서 확인했다.
- 핀 → GPIO 대응은 SDK `fsl_iomuxc.h` 에서 확인했다. 예: `IOMUXC_GPIO_EMC_B1_11_GPIO2_IO11 = 0x42A1003C, ALT5`.
- `GPIO_EMC_B1_39` 는 FlexSPI2 의 **보조 핀 그룹** CS(ALT3) 이기도 하다. 이 보드의 QSPI 는 **주 핀 그룹**(`GPIO_AON_22~27`)을 쓰므로 겹치지 않는다.
- RED 와 BLUE 는 같은 점퍼 반대쪽이 SRAMC 신호(SRAMC_ADDR00, SRAMC_CS1)다. SRAMC 소켓(J61)을 쓰면 LED 를 잃는다.
- D3 는 전원 LED 다. D10~D12 는 MCU-Link 상태 LED 다.

## 5. 디버그 UART

| UART | TX | RX | 연결 | 점퍼 |
|---|---|---|---|---|
| **LPUART1** | `GPIO_AON_08` | `GPIO_AON_09` | MCU-Link **VCOM 1** (항상 사용 가능) | J43, J41 1-2 (기본) |
| LPUART3 | `GPIO_AD_13` | `GPIO_AD_14` | MCU-Link VCOM 2 | J33, J27 1-2, J34 개방 |

- 두 UART 는 레벨 변환기 U30(74AVC4TD245)을 거친다.
- LPUART1 은 BootROM 시리얼 다운로더의 UART 이기도 하다.
- J34 의 기본값이 UM 안에서 엇갈린다. Table 4 는 개방이라 하고 Table 17 본문은 연결이라 한다. 보드에서 확인한다.

## 6. 외부 메모리

| 컨트롤러 | 부품 | 핀 | 비고 |
|---|---|---|---|
| **FlexSPI2 Port A** | **W25Q128JVSIQ** (U28) QSPI NOR 16 MB, 3.3 V | `GPIO_AON_22~27` (SS0 · CLK · D0~D3) | **기본 부트 플래시**, CM33 `0x0400_0000` |
| FlexSPI1 | W956A8MBYA5K (U18) HyperRAM 8 MB | 회로도 9페이지 (로드맵 27 에서 정리) | `0x2800_0000` |
| FlexSPI1 | S26HL512T (U24) HyperFlash 64 MB | — | **기본 미연결.** R183~185, R213~217, R231~233 을 1-3 으로 옮겨야 함 |

SDK FCB(`frdmimxrt1186_flexspi_nor_config.c`)는 W25Q128 을 이렇게 설정한다.

| 항목 | 값 |
|---|---|
| 읽기 | Quad I/O Fast Read `0xEB` |
| 클럭 | 100 MHz SDR |
| 샘플링 | Loopback from DQS pad |
| 크기 | 16 MB, 페이지 256 B, 섹터 4 KB, 블록 64 KB |

## 7. 전원

5 V 입력은 셋 중 하나를 **J4** 로 고른다.

| J4 | 전원 |
|---|---|
| 1-2 | DC 잭 J6 |
| 3-4 | USB OTG1 J63 |
| 5-6 | **MCU-Link USB J23** — 개발 중에는 이것 하나로 충분하다 |

J4 의 출하 위치는 UM 에 명시되어 있지 않다. 보드에서 확인해 여기에 적는다.

## 8. 네트워크 커넥터 (요약)

RJ45 가 4개(J56A/B, J57A/B)다. 기본 점퍼 상태에서는 **EtherCAT 2포트(J57A/B)** 가 연결되어 있다. 상세는 [05-network-overview.md](05-network-overview.md) 에 정리했다.

## 9. 확인 필요

- [ ] J4 출하 위치
- [ ] J34 출하 상태 (LPUART3 VCOM)
- [ ] SW4 버튼 극성 (풀업 + 누르면 Low 로 예상)
