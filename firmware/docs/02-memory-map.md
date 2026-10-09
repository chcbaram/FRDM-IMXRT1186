# 02. 메모리 맵

> CM33 과 CM7 이 각각 어떤 주소로 무엇을 보는지, 그리고 16 MB QSPI 를 어떻게 나눌지.
> 관련: [01-boot-sequence.md](01-boot-sequence.md) · [03-board-mapping.md](03-board-mapping.md)
>
> 출처: RM 3.2 System memory map (CM7) Table 4, 3.3 System memory map (CM33) Table 5, 12.4.1 Internal ROM/RAM Memory Map, 31.1.1 FlexSPI Table 194 / SDK `MIMXRT1186xxxxx_cm33_flexspi_nor.ld`, `MIMXRT1186xxxxx_cm7_ram.ld`.

![메모리 맵](images/memory-map.svg)

---

## 1. 먼저 알아야 할 것 — 같은 주소, 다른 메모리

**두 코어는 같은 주소로 서로 다른 메모리를 본다.** 가장 헷갈리는 부분이다.

| 주소 | CM33 에서 보면 | CM7 에서 보면 |
|---|---|---|
| `0x0000_0000` | CM33 BootROM | **CM7 ITCM** |
| `0x0FFE_0000` | **CM33 Code TCM** (128 KB) | — |
| `0x2000_0000` | **CM33 System TCM** (128 KB) | **CM7 DTCM** |
| `0x203C_0000` | CM7 ITCM (alias) | — |
| `0x2040_0000` | CM7 DTCM (alias) | — |
| `0x201E_0000` | — | CM33 Code TCM (alias) |
| `0x2020_0000` | — | CM33 System TCM (alias) |
| `0x2048_0000` | OCRAM1 | OCRAM1 (같음) |
| `0x0400_0000` | FlexSPI2 | FlexSPI2 (같음) |

그래서 코어 간 공유 데이터는 **양쪽에서 같은 주소로 보이는 OCRAM** 에 두는 것이 안전하다. 상대 코어의 TCM 에 쓰려면 alias 주소를 써야 한다. CM33 이 CM7 이미지를 CM7 ITCM 에 복사할 때는 `0x303C_0000`(Secure alias)에 쓴다. SDK `Prepare_CM7()` 이 이 주소를 쓴다.

## 2. CM33 주소 공간 (RM Table 5)

CM33 은 TrustZone 이 있어서 같은 메모리에 **NS 주소와 S 주소가 둘 다** 있다. S 주소는 NS 주소에 `0x1000_0000` 을 더한 값이다(28번 비트). 이 프로젝트는 SDK 와 같이 **NS 주소로 링크하고 Secure 상태로 실행**한다([01-boot-sequence.md](01-boot-sequence.md) 5절).

| NS 주소 | S 주소 | 크기 | 영역 |
|---|---|---|---|
| `0x0000_0000` | `0x1000_0000` | 128 + 32 KB | CM33 BootROM |
| `0x0200_0000` | `0x1200_0000` | 32 MB | FlexSPI1 앞 32 MB alias |
| **`0x0400_0000`** | `0x1400_0000` | 64 MB | **FlexSPI2 — QSPI NOR (부트 플래시)** |
| `0x0C00_0000` | `0x1C00_0000` | 32 MB | SEMC 코드 alias |
| **`0x0FFE_0000`** | `0x1FFE_0000` | 128 KB | **CM33 Code TCM** |
| **`0x2000_0000`** | `0x3000_0000` | 128 KB | **CM33 System TCM** |
| `0x203C_0000` | `0x303C_0000` | 256 KB | CM7 ITCM (alias) |
| `0x2040_0000` | `0x3040_0000` | 256 KB | CM7 DTCM (alias) |
| **`0x2048_0000`** | `0x3048_0000` | 512 KB | **OCRAM1** |
| `0x2050_0000` | `0x3050_0000` | 256 KB | OCRAM2 |
| `0x2200_0000` | `0x3200_0000` | 32 MB | FlexSPI2 앞 32 MB alias |
| **`0x2800_0000`** | `0x3800_0000` | 128 MB | **FlexSPI1 — HyperRAM 8 MB** |
| `0x4000_0000` ~ | `0x5000_0000` ~ | — | 주변장치 (AIPS1~4, GPIO, DAP …) |
| `0x6000_0000` | `0x7000_0000` | 16 MB | NETC 레지스터 |
| `0x8000_0000` | `0x9000_0000` | 256 MB | SEMC |
| `0xE000_0000` | — | — | Private Peripheral Bus (NVIC, SysTick, SCB) |

### 주변장치 주소는 둘이다

디바이스 헤더(`MIMXRT1186_cm33_COMMON.h`)는 `__ARM_FEATURE_CMSE` 값에 따라 베이스 주소를 고른다.

| 레지스터 | `-mcmse` (Secure) | 그 외 |
|---|---|---|
| `RGPIO2` | `0x5381_0000` | `0x4381_0000` |
| `IOMUXC` | `0x52A1_0000` | `0x42A1_0000` |
| `LPUART1` | `0x5438_0000` | `0x4438_0000` |
| `CCM` | `0x5445_0000` | `0x4445_0000` |

`fsl_iomuxc.h` 의 핀 매크로는 NS 주소(`0x42A1_xxxx`)로 고정돼 있다. 그래도 SAU 가 꺼져 있으면 Secure 로 접근되므로 동작한다. TrustZone 을 켜는 순간 이 가정이 깨지므로 그때 다시 정리한다.

## 3. 온칩 RAM 사용 시 주의

| 영역 | 주의 |
|---|---|
| OCRAM1 앞 16 KB (`0x2048_0000`~`0x2048_3FFF`) | SDK 링커 주석: *"TRDC 가 접근을 막는다."* SDK 와 probe-rs 모두 `0x2048_4000` 부터 쓴다 |
| OCRAM1 `0x3048_0000`~`0x3049_1FFF` (72 KB) | **ROM 이 부팅 중에 쓴다**(RM 12.4.1). non-XIP 이미지를 여기에 로드하면 안 된다. 부팅이 끝난 뒤에는 자유롭게 써도 된다 |
| CM33 TCM | ECC 가 켜져 있다. ROM 이 POR 때 0 으로 채운다 |
| CM7 TCM | ECC 가 켜져 있고, ROM 이 **채우지 않는다.** CM7 을 깨우기 전에 CM33 이 0 으로 채운다 |
| `0x0000_0000` 근처 | RM 12.5.1.2: *"non-XIP 이미지는 `0x0000_0000`~`0x0000_0003` 근처를 피하라"* |

TCM 크기는 CM7 의 ITCM/DTCM 분할(합계 512 KB)과 CM33 의 Code/System 분할(합계 256 KB)을 소프트웨어 설정 옵션으로 바꿀 수 있다(표의 *"if … SW config option …"*). 이 프로젝트는 **기본값(각 절반)** 을 쓴다.

## 4. 1차 링크 배치 (LED 단계)

SDK `MIMXRT1186xxxxx_cm33_flexspi_nor.ld` 의 기본 배치를 그대로 따른다.

| 섹션 | 영역 | 주소 |
|---|---|---|
| FCB | 플래시 | `0x0400_0400` |
| 컨테이너 | 플래시 | `0x0400_1000` |
| 벡터 테이블 | 플래시 | `0x0400_B000` |
| `.text` / `.rodata` | 플래시 (XIP) | `0x0400_B400` ~ |
| `.data` / `.bss` / 힙 / 스택 | CM33 System TCM | `0x2000_0000` ~ `0x2001_FFFF` |
| 빠른 코드 (`.ramfunc`) | CM33 Code TCM | `0x0FFE_0000` ~ |
| non-cacheable | OCRAM1 끝 | 로드맵 24 에서 결정 |

## 5. QSPI 파티션 계획

![QSPI 파티션](images/flash-partition.svg)

16 MB 를 아래와 같이 나눈다. **계획이며, 부트로더를 만드는 로드맵 40 에서 확정한다.** SDK 의 멀티코어 예제는 CM7 이미지를 `0x0480_0000`(8 MB 지점)에 둔다. 이 위치는 그대로 따른다.

| 주소 | 크기 | 용도 | 언제 |
|---|---|---|---|
| `0x0400_0000` | 256 KB | 부트 헤더(FCB·컨테이너) + **부트로더** | 1차에는 LED 펌웨어가 이 자리를 쓴다 |
| `0x0404_0000` | 4 KB | 앱 **TAG** (magic · 크기 · CRC32 · 버전) | 로드맵 42 |
| `0x0404_1000` | ~7.7 MB | **CM33 앱** | 로드맵 42 |
| `0x0480_0000` | 4 MB | **CM7 이미지** | 로드맵 44 |
| `0x04C0_0000` | 4 MB | 데이터 / NVS / 업데이트 임시 영역 | 필요할 때 |

### 왜 부트로더를 TCM 에서 돌리나

QSPI 는 **XIP 중에는 같은 칩을 지우거나 쓸 수 없다.** 플래시에서 실행 중인 코드가 그 플래시에 쓰면 읽기 버스가 멈춘다. 부트로더는 업데이트할 때 플래시를 써야 하므로, 컨테이너의 load address 를 **CM33 Code TCM** 으로 두어 ROM 이 복사해서 실행하게 만든다(non-XIP). 이 경우 128 KB 안에 들어가야 한다. 앱은 XIP 로 둔다.
