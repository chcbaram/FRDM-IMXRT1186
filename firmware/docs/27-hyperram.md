# 27. HyperRAM — XMCD 로 BootROM 이 연다

> 보드의 HyperRAM 8 MB(W956A8MBYA, FlexSPI1)를 `0x2800_0000` 에 붙인다. 설정은 BootROM 이 부트 헤더의 XMCD 를 보고 하고, 펌웨어는 확인과 시험만 한다.
> 관련: [02-memory-map.md](02-memory-map.md) · [03-board-mapping.md](03-board-mapping.md) 6절 · [25-cm7-boot.md](25-cm7-boot.md) 9절 (CM7 MPU) · [26-qspi-nor.md](26-qspi-nor.md)
>
> 출처: RM 12.7 External Memory Configuration Data (Table 90~94), SDK `frdmimxrt1186_flexspi_nor_config.c` (`USE_HYPERRAM`), `frdmimxrt1186_hyperram_txt_cm33.ini`, `driver_examples/flexspi/hyper_ram/polling_transfer`.

![HyperRAM 경로](images/hyperram-xmcd.svg)

---

## 1. 결과

```
[OK] psramInit()
     HyperRAM  8192 KB @ 0x28000000 (XMCD)
cli# psram test
data bus (walking 1) : OK
addr bus (2^n)       : OK
full 8192 KB (addr, ~addr) : OK  (0 words differ)
  write 32bit loop   :  341122 us  (24014 KB/s)
  read  32bit loop   :  211154 us  (38796 KB/s)
  memset 1 MB        :  166052 us  (6166 KB/s)
  memcpy 1 MB        :  172420 us  (5938 KB/s)
```

- 8 MB 전체를 주소 값과 그 반전으로 두 번 채워 비교했다. 불일치는 0 이다.
- FlexSPI1 루트 클럭은 332 MHz 다. HyperRAM 은 DDR 이라 166 MHz 로 돈다.

## 2. 왜 XMCD 인가

HyperRAM 을 붙이는 길은 두 가지다.

| 방법 | 내용 |
|---|---|
| **XMCD** (채택) | 부트 헤더 `+0x800` 에 3 워드를 적는다. **BootROM 이 부팅할 때** 핀 · 클럭 · LUT 를 잡고 크기를 자동으로 감지한다. SDK 보드의 `USE_HYPERRAM` 빌드와 같다 |
| SDK `fsl_flexspi` | 펌웨어가 핀 11 개, 클럭, LUT(읽기 0xA0 · 쓰기 0x20 · 레지스터 0xE0/0x60)를 직접 설정한다. `hyper_ram/polling_transfer` 예제 |

우리 링커와 부트 헤더에는 처음부터 XMCD 자리(`.boot_hdr.xmcd_data`, `0x0400_0800`)가 비어 있었다([20](20-led.md) 3절). 3 워드만 채우면 되고, 핀 · 클럭 설정을 펌웨어가 가질 필요가 없다.

### XMCD 값 (`bsp/boot/boot_hdr.c`)

| 워드 | 값 | 필드 (RM Table 91 / 93 / 94) |
|---|---|---|
| 헤더 | `0xC001000C` | tag `C`, FlexSPI, **인스턴스 1**, 간이 설정 블록, 크기 12 B |
| 옵션 0 | `0xC1001800` | tag `C`, 옵션 2 워드, **HyperRAM**, single-ended 클럭, **166 MHz**, 크기 0 = 자동 |
| 옵션 1 | `0x10000000` | **포트 B**, DQS · 핀 그룹 기본(primary), 더미 사이클 자동 |

XMCD CRC 검사는 퓨즈(`BOOT_CFG7[20] XMC_CRC32_CHECK_EN`)가 0 이라 하지 않는다.

### ROM 이 잡은 것 (보드에서 읽음)

| 항목 | 값 |
|---|---|
| FlexSPI1 `MCR0` | `0xFFFF_A030` (MDIS = 0, 켜짐) |
| 포트 B1 크기 `FLSHB1CR0` | `0x2000` KB = **8 MB** (자동 감지) |
| 핀 | `GPIO_SD_B2_00~03 · 05~11` mux ALT1(FLEXSPI1 포트 B). DQS(`SD_B2_05`)와 SCLK(`SD_B2_07`)는 SION 까지 `0x11` |
| 루트 클럭 | 332 MHz (`psram info`) |

`SD_B2_04` 는 mux 5(GPIO, 리셋 기본값) 그대로다. ROM 이 잡지 않는다. SDK 디버그 스크립트(`.ini`)의 핀 목록에도 없다.

## 3. 펌웨어는 확인만 한다 — `psram.c`

`psramInit()` 은 FlexSPI1 을 **설정하지 않는다.** ROM 이 열어 두었는지만 본다.

1. `FLEXSPI1->MCR0` 의 MDIS 가 0 인지(켜졌는지)
2. `FLSHCR0[B1]` 에 크기가 적혔는지. 이 값을 `psramGetLength()` 로 쓴다
3. 처음 · 가운데 · 끝 워드에 두 패턴을 써서 읽어 본다(원래 값은 되돌린다)

ROM 이 설정하지 않았으면 `[E_] psramInit()` 으로 드러난다. API 는 예전 저장소(Teensy41 의 RT1062 PSRAM 등)의 `psram.h` 와 같다: `psramInit` · `psramIsInit` · `psramGetAddr` · `psramGetLength`.

### 직접 초기화는 언제 필요한가

ROM 은 전원 재인가 · SW3(POR_B) · SYSRESETREQ(`probe-rs` 리셋, 소프트 리셋)마다 다시 돌아 XMCD 도 매번 적용된다. 그래서 지금은 직접 초기화가 필요 없다. 다음 경우에는 `fsl_flexspi` + LUT 로 직접 초기화를 넣는다.

- 저전력 모드에서 FlexSPI1 · HyperRAM 전원을 내렸다 올릴 때
- 주파수나 타이밍을 실행 중에 바꿀 때 (XMCD 는 166 MHz 고정)
- ROM 을 거치지 않는 실행 경로가 생길 때 (디버거로 RAM 에 바로 올려 실행 등)

## 4. 두 코어의 캐시 규칙

| | CM33 | CM7 |
|---|---|---|
| 캐시 | **하지 않는다.** 시스템 버스 캐시 `XCACHE_PS` 가 꺼져 있다([25](25-cm7-boot.md) 9절) | **한다.** MPU 영역 8 `0x2800_0000` 8 MB Normal write-back (`cm7/bsp/mpu.c`) |
| 결과 | 바이트 접근마다 버스를 탄다. newlib-nano 의 `memset` · `memcpy` 는 크기 우선이라 바이트 루프여서 6 MB/s 다. 32 bit 루프는 24 / 39 MB/s | 빠르지만 CM33 이 쓴 값을 늦게 본다 |

### CM7 캐시 확인 (`peer read`)

CM7 이 직접 읽도록 cmd 명령 `SHARED_CMD_MEM_READ` 와 CLI `peer read addr` 을 더했다([25](25-cm7-boot.md) 11절의 패킷 통신을 쓴다).

| 순서 | 메모리 실제 값 | CM7 이 읽은 값 |
|---|---|---|
| CM33 쪽에서 `0x2820_0000` 에 `0x11111111` 을 씀 | `0x11111111` | `0x11111111` — 이때 캐시에 줄이 들어온다 |
| CM33 쪽에서 `0x22222222` 로 바꿈 | `0x22222222` | **`0x11111111`** — 캐시에 남은 옛 값 |
| 공유 블록 `0x2050_0000` (non-cacheable) | magic | `0x48535452` — 그대로 맞다 |

**HyperRAM 으로 두 코어가 데이터를 주고받으려면 CM7 쪽이 캐시를 다뤄야 한다.**

- CM7 이 쓴 것을 CM33 이 읽게 하려면, 쓴 뒤 `SCB_CleanDCache_by_Addr()` 를 부른다.
- CM33 이 쓴 것을 CM7 이 읽게 하려면, 읽기 전에 `SCB_InvalidateDCache_by_Addr()` 를 부른다.
- 구간은 32 B(캐시 줄) 단위로 맞춘다.
- 아니면 주고받는 구간만 MPU 로 non-cacheable 로 따로 잡는다.

코어 간 신호와 작은 데이터는 지금처럼 OCRAM2 공유 블록(non-cacheable)과 cmd 패킷으로 하고, HyperRAM 에는 큰 버퍼를 두고 위치만 패킷으로 넘긴다([25](25-cm7-boot.md) 11절).

## 5. CLI

```
psram info     init, 주소, 크기(FLSHB1CR0), FlexSPI1 MCR0 · 루트 클럭
psram test     데이터 버스, 주소 버스, 8 MB 전체 비교, 속도  ← 8 MB 를 덮어쓴다
peer read addr CM7 이 그 주소를 읽는다 (CM7 의 캐시를 거친다)
```

## 6. 다음

- [ ] CM33 `XCACHE_PS` 를 켤지 정한다. 켜면 HyperRAM 이 빨라지지만 OCRAM2 공유 블록도 캐시되므로, CM33 MPU 로 공유 구간을 non-cacheable 로 먼저 잡아야 한다
- [ ] newlib-nano 대신 워드 단위 `memcpy` · `memset` 을 쓸지 정한다 (HyperRAM · OCRAM 같은 느린 메모리에서 차이가 크다)
- [ ] 링커에 HyperRAM 섹션(`.psram`, NOLOAD)을 둔다. 큰 버퍼를 이름으로 놓을 수 있게 (28 RTOS 힙, 50 번대 네트워크 버퍼)
