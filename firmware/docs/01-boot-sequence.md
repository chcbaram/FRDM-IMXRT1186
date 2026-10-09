# 01. i.MX RT1180 부팅 시퀀스

> 전원이 들어온 뒤 BootROM 이 FlexSPI2 의 QSPI NOR 에서 이미지를 찾아 CM33 의 `Reset_Handler` 로 뛰기까지, 그리고 CM7 이 깨어나는 경로.
> 관련: [02-memory-map.md](02-memory-map.md) · [03-board-mapping.md](03-board-mapping.md)
>
> 출처: i.MX RT1180 Reference Manual Rev.10 (이하 RM) 12장 System Boot — 12.1, 12.3, 12.4, 12.5.1, 12.6, 12.7 / MCUXpresso SDK `mcux-devices-rt` `RT1180/MIMXRT1189/xip/fsl_flexspi_nor_boot.[ch]`, `system_MIMXRT1186_cm33.c`.

![부팅 시퀀스](images/boot-sequence.svg)

---

## 1. 한 눈에

이 칩에는 **내장 유저 플래시가 없다.** 코드는 항상 외부 메모리에 있고, 리셋되면 반드시 **CM33 의 온칩 BootROM** 이 먼저 돈다. STM32N6 과 같은 구조다. 다른 점은 다음 두 가지다.

| 항목 | STM32N6 | i.MX RT1180 |
|---|---|---|
| 첫 사용자 코드 | FSBL 을 SRAM 으로 복사해 실행 | **XIP 가능** — 플래시에서 바로 실행해도 된다 |
| 이미지 헤더 | STM32 헤더 v2.3 (서명 도구 필요) | **FCB + 컨테이너** — 구조체라서 C 로 직접 만들 수 있다 |
| 보안 엔진 | — | **EdgeLock Enclave (ELE)** 가 컨테이너를 검증 |

부트 코어는 **CM33** 이다(RM 12.1). CM7 은 리셋에 묶인 채로 시작하고 CM33 이 풀어 준다(5절).

## 2. 부트 모드 결정

BootROM 은 `SBMR2[BOOT_MODE]` 를 읽는다. 이 값은 **POR_B 상승 에지에서 BOOT_MODE[2:0] 핀을 샘플**한 것이고, 그 뒤 핀을 바꿔도 변하지 않는다(RM 12.3.3.1). 즉 **딥스위치를 바꾼 뒤에는 POR 이 필요하다.** 이 보드는 SW2 리셋 버튼(J24 단락, 기본)과 MCU-Link 리셋(J26 단락, 기본)이 모두 MCU **power-on reset** 을 건다(UM12450 점퍼 표의 J24, J26). 그래서 SW2 를 누르거나 디버거로 리셋하면 BOOT_MODE 를 다시 읽는다.

| BOOT_MODE[2:0] | 동작 (RM Table 52) |
|---|---|
| `000` | Boot From Internal Fuses — `BT_FUSE_SEL=0` 이면 시리얼 다운로더 |
| `001` | Serial Downloader (USB-HID / LPUART1 / LPSPI1) |
| `010` | eMMC 8bit (uSDHC2) |
| `011` | SD 4bit (uSDHC1) |
| **`100`** | **Serial NOR via FlexSPI** — 이 프로젝트가 쓰는 모드 |
| `101` | Serial NAND 2K page |
| `110` | Infinite Loop — 디버거를 붙이기 좋은 모드 |
| `111` | Test (NXP 전용) |

판정 순서(RM Figure 36):

1. ROM API 로 부트 모드가 지정됐고 `DIS_BT_MODE_API=0` 이면 그 값을 쓴다.
2. 퓨즈 `FORCE_BT_FROM_FUSE=1` 이면 `BOOT_MODE_FROM_FUSE` 를 쓴다.
3. 핀이 `000` 이면 `BT_FUSE_SEL` 에 따라 퓨즈 부팅 또는 시리얼 다운로더.
4. 핀이 `110`/`111` 이면 Infinite Loop (퓨즈로 막을 수 있다).
5. 나머지는 핀 값 그대로.

### ⚠️ `100` 의 기본 인스턴스는 FlexSPI1 이다

RM Table 52 의 각주가 중요하다. `100` 은 **"Serial NOR via FlexSPI1, Primary group, PortA"** 이고 *"부트 장치만 고정이며 인스턴스·핀 그룹·포트는 BOOT_CFG 퓨즈로 덮어쓸 수 있다"*.

| 퓨즈 | 이름 | 출하값 | 의미 |
|---|---|---|---|
| `BOOT_CFG2[7]` | `XSPI_INSTANCE` | 0 | 0 = FlexSPI1, **1 = FlexSPI2** |
| `BOOT_CFG2[6]` | `XSPI_PIN_GROUP_SEL` | 0 | 0 = Primary 핀 그룹 |
| `BOOT_CFG1[7]` | `XSPI_NOR_CONNECTION_SEL` | 0 | 0 = PORTA CS0 |
| `BOOT_CFG1[19:18]` | `XSPI_NOR_FCB_OFFSET` | 0 | 0 = FCB 가 `0x400` |

그런데 이 보드의 부트 플래시(W25Q128, U28)는 **FlexSPI2** 에 물려 있다([03-board-mapping.md](03-board-mapping.md)). 출하 퓨즈 그대로라면 ROM 은 FlexSPI1(HyperFlash 자리, 기본 미연결)을 찾아야 한다. 그런데도 공장 데모가 QSPI 에서 부팅되므로, **FRDM 보드 칩은 `XSPI_INSTANCE` 퓨즈가 이미 구워져 나왔을 가능성이 높다.** 이것은 **확인 필요** 항목이다. 첫 부팅이 되면 OCOTP 퓨즈 섀도를 읽어 이 문서에 기록한다.

## 3. ROM 이 FlexSPI NOR 에서 하는 일

RM 12.5.1.2 와 Figure 39 를 순서대로 정리하면 다음과 같다.

1. FlexSPI 핀먹스를 걸고 **30 MHz 로 1-bit 읽기**(명령 `0x03`)를 한다.
2. 플래시 `+0x400` 에서 **FCB(Flash Configuration Block, 512 B)** 를 읽는다. FCB 대신 JESD216(SFDP) 자동 탐지로 파라미터를 만들 수도 있다(`XSPI_NOR_PROBE_TYPE`).
3. FCB 값으로 FlexSPI 를 다시 설정한다. 설정 항목은 LUT, 패드 수, 클럭, 샘플링이다. 이때부터 AHB 로 플래시를 읽을 수 있다.
4. `+0x800` 의 **XMCD** 가 유효하면 HyperRAM/SDRAM 을 설정한다. 우리는 쓰지 않는다.
5. `+0x1000` 의 **컨테이너**를 읽고 ELE 로 헤더를 검증한다(4절).
6. 이미지가 **XIP**(load address 가 플래시 주소)면 그대로 두고, **non-XIP** 면 load address 로 복사한다.
7. 컨테이너의 entry point 로 점프한다.

실패하면 `ImageIndex` 를 올려 재시도하거나 다음 부트 대안으로 넘어간다. 모든 대안이 실패하면 **시리얼 다운로더**로 떨어진다(RM 12.3.1).

### ROM 이 남기고 가는 상태

RM 12.4.4 와 12.4.5 에 따르면 ROM 은 아래 상태를 **되돌려 놓지 않는다.** 앱은 이 값들이 리셋 기본값이라고 가정하면 안 된다.

| 대상 | ROM 실행 후 상태 |
|---|---|
| CM33 클럭 | `BOOT_FREQ=0` 이면 **RCOSC200M 200 MHz**, `=1` 이면 PLL3 240 MHz |
| Bus AON / WAKEUP | 100 MHz (RCOSC200M) |
| FlexSPI2 | 부팅에 쓴 설정 그대로. 클럭원은 PLL3_PFD2 |
| ANADIG PLL/OSC/PMU, CCM | 부팅에 쓴 설정 그대로 |
| WDOG1~5 | ROM 이 끔. `WDOG_EN` 퓨즈를 구웠으면 WDOG1 이 켜진 채로 넘어온다 |
| SRC GPR0~4, GPR9 | ROM 이 사용 (persistent bits, RM Table 56) |
| CM33 TCM | POR 이면 0 으로 프리로드한다. **TCM ECC 가 기본 켜져 있기 때문이다**(12.4.3) |

> **TCM ECC 함정**: 이 칩의 TCM 은 ECC 가 켜진 상태로 나온다. 쓰지 않은 TCM 을 읽으면 ECC 오류가 난다. CM33 TCM 은 ROM 이 POR 때 0 으로 채워 주지만 **CM7 TCM 은 기본으로 채우지 않는다**(`POR_PRELOAD_M7_TCM_ECC_EN=0`). CM7 을 깨울 때 우리가 지워야 한다(5절).

## 4. 부트 이미지 구조

플래시 맨 앞 4 KB 는 **메모리 설정 블록**이다. 이미지는 `0x1000` 부터 시작한다(RM Figure 50, Table 81).

![플래시 이미지 배치](images/flash-image-layout.svg)

| 플래시 오프셋 | 크기 | 내용 | 이 프로젝트 |
|---|---|---|---|
| `0x0000` | 0x200 | OTFAD KeyBlob (암호화 XIP 용) | 0 으로 채움 |
| `0x0400` | 0x200 | **FCB** — FlexSPI NOR 설정 블록, tag `"FCFB"` | **필수** |
| `0x0800` | 0x200 | XMCD — 외부 RAM 설정 | 0 (비활성) |
| `0x1000` | ≤ 0x2000 | **컨테이너** 헤더 + 이미지 배열 + 서명 블록 | **필수** |
| `0xB000` | — | CM33 이미지 (벡터 테이블부터) | SDK 와 같은 오프셋 |

### 컨테이너 헤더 (RM Table 82)

| 오프셋 | 필드 | 값 |
|---|---|---|
| `0x00` | Version / Length / **Tag** | `0x00` / 컨테이너 크기 / **`0x87`** |
| `0x04` | Flags | `0x0000_0000` — SRK Set=0, 즉 **인증하지 않는 컨테이너** |
| `0x08` | SW ver / Fuse ver / # of images | 0 / 0 / 1 |
| `0x0C` | Signature block offset | 헤더 + 이미지 엔트리 뒤 |

### 이미지 배열 엔트리 (RM Table 84, 85)

| 오프셋 | 필드 | 값 |
|---|---|---|
| `0x00` | Image Offset | **컨테이너 헤더 기준** 이미지 시작 = `0xB000 - 0x1000 = 0xA000` |
| `0x04` | Image Size | 링커 심볼로 계산 |
| `0x08` | Load Address (64 bit) | XIP 이면 벡터 테이블의 플래시 주소 `0x0400_B000` |
| `0x10` | Entry Point (64 bit) | 같은 값. ROM 이 여기서 SP/PC 를 읽는다 |
| `0x18` | Flags | **`0x213`** = 실행 이미지(3) · CM33(1<<4) · SHA512(2<<8) |
| `0x20` | Hash (512 bit) | 0 — 아래 참고 |

### 서명 없이 부팅되는 이유

RM 12.1 에 이렇게 적혀 있다. *"출하 시 LIFE_CYCLE 은 OEM Open 이다. ROM/AHAB 가 인증을 수행하지만 **인증 오류는 모두 무시되고 이미지는 실행된다**."* 그래서 개발 단계에서는 서명 도구(spsdk 등) 없이 **C 구조체로 만든 헤더만으로 부팅된다.** SDK 의 `fsl_flexspi_nor_boot.c` 도 해시를 0 으로 비워 둔다.

> ⚠️ RM 12.6.2 NOTE: *"컨테이너에 이미지가 **여러 개**면 ELE 가 각 이미지의 해시를 검증하고, 실패하면 무한 리셋 루프에 빠진다."* CM33+CM7 두 이미지를 한 컨테이너에 넣는 단계(로드맵 44)에서는 SHA512 를 빌드에서 계산해 넣어야 한다.

> ⚠️ LIFE_CYCLE 을 OEM Closed 로 올리거나 SRK 퓨즈를 굽는 것은 **되돌릴 수 없다.** 보안 부트는 로드맵 48 에서 문서로만 먼저 다룬다.

## 5. CM33 진입 이후

ROM 은 컨테이너 entry 의 벡터 테이블에서 SP 와 PC 를 읽고 **Secure 상태로** `Reset_Handler` 에 들어간다. 이후는 우리 코드다.

| 순서 | 위치 | 하는 일 |
|---|---|---|
| 1 | `Reset_Handler` (startup .S) | 인터럽트 마스크, `VTOR` ← 벡터 테이블, `MSP`/`MSPLIM` 설정 |
| 2 | `SystemInit()` | FPU 허용, **RTWDOG1~5 끔**, ROM 이 켠 SysTick 끔, **XCACHE_PC(코드 캐시) 켬** |
| 3 | startup | `.data` 복사, `.bss` 0 클리어 |
| 4 | `main()` | `bspInit → hwInit → apInit → apMain` |

`XCACHE_PC` 는 FlexSPI XIP 코드 페치용 캐시다. 끄면 플래시에서 도는 코드가 매우 느려진다.

### 주소는 Non-secure alias 인데 Secure 로 돈다

SDK 링커는 플래시를 `0x0400_0000`, DTCM 을 `0x2000_0000` 에 둔다. 메모리맵 표(RM Table 5)에서는 이 주소들이 **NS alias** 이고 Secure alias 는 `0x1400_0000`, `0x3000_0000` 이다. 그런데 SAU 가 꺼져 있으면(`SAU_CTRL.ENABLE=0`, `ALLNS=0`) **모든 주소가 Secure 로 취급**된다. 그래서 NS alias 주소에서도 Secure 코드가 문제없이 돈다. 레지스터 베이스는 `-mcmse` 로 빌드하면 디바이스 헤더가 Secure 베이스(`0x5xxx_xxxx`)를 고른다. TrustZone 분할은 이 프로젝트 범위 밖이며 [02-memory-map.md](02-memory-map.md) 에서 정리한다.

## 6. CM7 기동

CM7 은 리셋이 유지된 채로 시작한다(`RELEASE_M7_RST_STAT=0` 출하값). 깨우는 쪽은 CM33 이다. SDK `system_MIMXRT1186_cm33.c` 의 `Prepare_CM7()` 순서를 따른다.

| 단계 | 레지스터 | 내용 |
|---|---|---|
| 1 | `PHY_LDO`, `ANADIG_PLL->ARM_PLL_CTRL`, `DCDC->REG3` | CM7 전원/ARM PLL 준비 |
| 2 | `BLK_CTRL_S_AONMIX->M7_CFG.INITVTOR` | CM7 초기 벡터 주소 (`>> 7`, 128 B 정렬) |
| 3 | `SRC_GENERAL_REG->SCR = BT_RELEASE_M7` | CM7 리셋 해제 |
| 4 | DMA4 로 `0x303C_0000` ~ `0x3043_FFFF` 0 채움 | CM7 ITCM/DTCM ECC 초기화 |

ROM 이 직접 CM7 을 풀게 하는 방법도 있다. `RELEASE_M7_RST_STAT=1` 퓨즈를 구우면 되지만, 그러면 CM7 이 항상 주소 `0x0` 에서 출발한다(RM 12.4.3). **퓨즈를 굽지 않는 방식(CM33 이 해제)을 쓴다.** 상세는 로드맵 25 에서 `04-dualcore.md` 로 정리한다.

## 7. 부팅이 안 될 때 보는 곳

| 증상 | 확인 |
|---|---|
| 아무 반응 없음 | J60 이 `100` 인가, SW2 나 전원 재인가로 POR 을 걸었나 |
| ROM 이 시리얼 다운로더로 빠짐 | FCB tag `FCFB` 위치(`+0x400`), 컨테이너 tag `0x87` 위치(`+0x1000`), 인스턴스 퓨즈 |
| 디버거를 붙이고 싶은데 앱이 바로 죽음 | J60 을 `110`(Infinite Loop)으로 두고 POR → 디버거에서 RAM 로드 |
| 실패 원인 추적 | `SRC GPR9[15:0]` BOOT_STAGE 를 디버거로 읽는다 (RM Table 56) |

## 8. 확인 필요

- [ ] 이 보드의 `BOOT_CFG2[7] XSPI_INSTANCE` 퓨즈 값 (FlexSPI2 로 부팅되는 실제 근거)
- [ ] `BOOT_FREQ` 퓨즈 값 — 앱 진입 시 CM33 이 200 MHz 인지 240 MHz 인지
