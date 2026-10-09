# 10. 개발 환경

> 제조사 IDE 와 SDK 설치 없이 **gcc + CMake + Ninja + probe-rs** 만으로 빌드, 기록, 디버그까지 한다. 무엇을 왜 골랐는지 정리한다. 설치 절차는 [13-os-setup.md](13-os-setup.md) 에 있다.
> 관련: [11-sdk-vendoring.md](11-sdk-vendoring.md) · [12-project-skeleton.md](12-project-skeleton.md)

---

## 0. 결론

| 도구 | 검증 버전 | 최소 | 역할 |
|---|---|---|---|
| Arm GNU Toolchain (`arm-none-eabi-gcc`) | 15.3.Rel1 | 13.2 | 컴파일러 |
| CMake | 4.4.3 | 3.20 | 빌드 구성 |
| Ninja | 1.13.2 | 1.10 | 빌드 실행 |
| **probe-rs** | 0.32.0 | 0.30 | **기록 + 디버그 서버** (MIMXRT1180 내장) |
| Python | 3.9 | 3.8 | 보조 스크립트 (표준 라이브러리만) |
| VSCode + `probe-rs.probe-rs-debugger` + `ms-vscode.cpptools` | — | — | 편집 · 디버그 |

설치하지 않는 것: MCUXpresso IDE, MCUXpresso SDK(west), LinkServer, SEGGER J-Link, MCUXpresso Config Tools, spsdk.

새 PC 에서 처음 할 일은 하나다.

```bash
python3 tools/setup_tools.py        # 도구 버전 점검 + 프로브 확인 + SVD 준비
```

## 1. 디버그 프로브 도구를 고른 과정

보드의 MCU-Link(LPC55S69)는 **CMSIS-DAP 펌웨어**로 출하된다. CMSIS-DAP 를 말하는 도구 중에서 RT1180 을 실제로 다룰 수 있는 것을 찾았다.

| 도구 | RT1180 | 판단 |
|---|---|---|
| **probe-rs** | `MIMXRT1180.yaml` 내장. CM33 코어, FlexSPI1/2 QSPI 기록 알고리즘 포함 | **채택.** 단일 바이너리이고 3 OS 를 모두 지원한다. 팩 파일이 필요 없다 |
| pyOCD | 내장 타깃 없음 (`target_MIMXRT1176` 까지). CMSIS 팩으로 시도할 수는 있다 | titan-mini 에서 팩 결함을 고치느라 고생했다. 보류 |
| OpenOCD | RT1180 지원이 불확실하다 | 보류 |
| LinkServer (NXP) | 공식 지원 | 제조사 툴이라 쓰지 않는다 |
| J-Link | 공식 지원 | MCU-Link 펌웨어를 바꿔야 하고, SEGGER 소프트웨어를 설치해야 한다 |

### probe-rs 의 한계 (지금 알고 있는 것)

- 타깃 정의에 **CM33 코어만 있다.** CM7 은 AP 2 에 있는데(DFP 팩 `__ap="2"`) 정의되어 있지 않다. CM7 을 디버깅하는 단계(로드맵 25)에서 커스텀 타깃 yaml 을 만든다.
- VSCode 에서는 **DAP 서버**(`probe-rs dap-server`)를 쓰는 `probe-rs-debugger` 확장으로 디버깅한다. `probe-rs gdb` 로 GDB 서버도 띄울 수 있어서, `arm-none-eabi-gdb` 를 직접 붙이는 길도 열려 있다.

## 2. SDK 는 설치하지 않고 골라 온다

MCUXpresso SDK 는 GitHub 에 공개되어 있다(BSD-3). 이 프로젝트는 SDK 를 통째로 받지 않는다. 필요한 파일만 **커밋 SHA 를 고정해서** 받아 저장소에 넣는다. 상세는 [11-sdk-vendoring.md](11-sdk-vendoring.md) 에 있다.

- 평소 빌드에는 네트워크가 필요 없다.
- 다른 PC 에서 `python3 tools/fetch_nxp_sdk.py --check` 를 돌리면 같은 파일인지 확인할 수 있다.

## 3. 저장소에 넣지 않는 것

| 자산 | 크기 | 위치 | 받는 법 |
|---|---|---|---|
| SVD (`MIMXRT1186_cm33.xml`, `_cm7.xml`) | 각 71 MB | `firmware/rt1180-fw/tools/svd/` | `setup_tools.py` 가 NXP 공개 CMSIS 팩(`MIMXRT1186_DFP 26.09.00`)에서 꺼낸다 |
| RM, 데이터시트 | 73 MB | `hardware/ref/` | NXP 사이트 (로그인 필요). 재배포 불가 |
| 공장 데모 이미지 백업 | 64 KB | `hardware/ref/factory/` | 첫 기록 전에 probe-rs 로 읽어 둔 것 ([20-led.md](20-led.md)) |

## 4. 보드 연결

| 항목 | 설정 |
|---|---|
| USB | **J23** (MCU-Link). 전원도 여기서 받는다 (J4 5-6) |
| J60 | `100` (QSPI 부팅) |
| J36, J58 | 개방 (기본) — 온보드 MCU-Link 가 온보드 MCU 를 디버깅 |
| J24, J26 | 단락 (기본) — SW2 와 MCU-Link TRG_RST 가 POR 로 이어진다. `probe-rs reset` 은 SYSRESETREQ 라 이 경로가 아니다 |

`probe-rs list` 결과는 다음과 같다.

```
[0]: MCU-LINK on-board (r0E2) CMSIS-DAP V3.156 -- 1fc9:0143-0:N1APBXYNYYR5I (CMSIS-DAP)
```

## 5. 자주 쓰는 명령

```bash
cd firmware/rt1180-fw
cmake -S . -B build -G Ninja
cmake --build build                     # elf / bin / map → build/cm33/
cmake --build build --target flash      # probe-rs download --verify + reset

probe-rs read --chip MIMXRT1180 b32 0x04001000 8    # 플래시/레지스터 읽기 (실행 중에도 된다)
probe-rs reset --chip MIMXRT1180
```

`probe-rs read` 는 코어를 멈추지 않고 AHB-AP 로 읽는다. 레지스터 값을 확인하는 데 가장 빠른 방법이다. [20-led.md](20-led.md) 의 검증이 전부 이 방법이다.

## 6. 플래시 기록 원리 — 디버거는 QSPI 에 직접 쓰지 않는다

![플래시 기록 경로](images/flash-download.svg)

SWD 로 할 수 있는 일은 AHB-AP 를 통한 **메모리 읽기/쓰기**와 **코어 제어**뿐이다. 그런데 `0x0400_0000`(FlexSPI2)은 읽기 전용 AHB 창이다. 플래시에 쓰려면 FlexSPI 컨트롤러로 쓰기 허가 → 지우기 → 페이지 쓰기 명령을 보내야 한다. probe-rs 는 이 일을 **칩 안에서 돌아가는 작은 프로그램(플래시 알고리즘)** 에 맡긴다. CMSIS-Pack 의 FLM 과 같은 방식이다.

### 6-1. 알고리즘 — `MIMXRT1180.yaml` 의 `flexspi2_qspi_cm33`

| 항목 | 값 |
|---|---|
| 적재 위치 | `0x2000_0008` (CM33 DTCM), 1580 B |
| 함수 | `Init +0x1`, `EraseSector +0x225`, `ProgramPage +0x255`, `EraseAll +0x209`, `UnInit +0x205` |
| 데이터 버퍼 | `+0x410` |
| 지우기 / 쓰기 단위 | 4 KB 섹터 / 256 B 페이지 |
| 스택 | 4 KB. yaml 주석: *"이렇게 안 하면 BootROM 의 스택이 알고리즘 코드를 덮어쓴다"* |

함수를 호출하는 방법은 다음과 같다.

1. 레지스터 R0~R3 에 인자를 넣는다.
2. LR 은 BKPT 명령을 가리키게 하고, PC 는 함수 주소에 둔 뒤 코어를 재개한다.
3. BKPT 에 걸려 멈추면 R0 의 반환값을 읽는다.

### 6-2. 알고리즘은 BootROM API 를 부르는 껍데기다

yaml 의 `instructions` 를 base64 로 풀어 역어셈블했다(`arm-none-eabi-objdump -D -b binary -marm -Mforce-thumb`). 하는 일은 두 가지다.

1. `0x1000_001C` 에서 ROM API 트리 포인터를 읽는다. SDK `fsl_romapi.c` 의 `ROM_API_Init()` 과 같은 코드다(`*(uint32_t *)0x1000001C`).
2. 트리의 `flexSpiNorDriver` 함수 테이블로 점프한다.

보드에서 읽은 실제 값은 다음과 같다.

```
$ probe-rs read --chip MIMXRT1180 b32 0x1000001C 1
1000001c: 10001bac                                  ← bootloader_api_entry_t
$ probe-rs read --chip MIMXRT1180 b32 0x10001bac 4
10001bac: 1001bc31 4b030001 10000444 10000400       ← runBootloader, 버전 K3.0.1, 저작권 문자열, flexSpiNorDriver
$ probe-rs read --chip MIMXRT1180 b32 0x10000400 14
10000400: 00010803 1000988d 10009be1 10009f5d 1000cd59 1000cde5 100091b3 10008f6d
10000420: 10008eed 1000aa89 1000a2ab 1000a44f 00000000 100091d5
```

`0x1000_0444` 의 문자열은 *"(c) Copyright 2024, NXP Semiconductor. All rights reserved."* 다.

| 드라이버 오프셋 | ROM 함수 | 주소 | 알고리즘에서 |
|---|---|---|---|
| `+0x00` | version | `0x0001_0803` | — |
| `+0x04` | `init` | `0x1000_988D` | Init |
| `+0x08` | `page_program` | `0x1000_9BE1` | ProgramPage (페이지 단위로 반복) |
| `+0x0C` | `erase_all` | `0x1000_9F5D` | EraseAll |
| `+0x10` | `erase` | `0x1000_CD59` | EraseSector |
| `+0x24` | `get_config` | `0x1000_AA89` | Init |

오프셋 해석은 SDK `fsl_romapi.c` 의 `flexspi_nor_driver_interface_t` 를 따른다. 원본은 `RT1180/MIMXRT1189/drivers/romapi` 다([11-sdk-vendoring.md](11-sdk-vendoring.md)).

`Init` 은 ROM API 를 부르기 전에 다음 준비를 한다.

- 워치독 4개를 끈다.
- 캐시 설정을 정리한다.
- FlexSPI2 클럭 루트(CCM root 22, `0x4445_0B00`)를 0 으로 되돌린다.

### 6-3. probe-rs 는 우리 FCB 를 쓰지 않는다

`Init` 은 `get_config(instance=2, &config, &option)` 에 옵션 워드 **`0xC000_0005`** 를 넘긴다. ROM 은 이 옵션으로 플래시의 **SFDP(JESD216)** 를 읽어 설정 블록을 스스로 만든다. SDK `serial_nor_config_option_t` 로 해석하면 다음과 같다.

| 필드 | 값 | 의미 |
|---|---|---|
| tag [31:28] | `0xC` | 옵션 워드 표식 |
| device_type [23:20] | 0 | QuadSPI SDR |
| query_pads [19:16] | 0 | SFDP 를 1 pad 로 읽는다 |
| max_freq [3:0] | 5 | 100 MHz |

RM 12.9.2.11.1 의 예시 *"QuadSPI NOR - Quad SDR Read: option0 = 0xc0000007 (133MHz)"* 와 같은 형식이다.

**결과적으로, 우리 FCB(`boot_hdr.c`)가 틀려도 기록은 성공한다. 부팅만 안 된다.** FCB 는 BootROM 이 부팅할 때만 읽는다. 그래서 FCB 를 고친 뒤의 검증은 기록 성공이 아니라 **POR 후 부팅**으로 한다.

### 6-4. 실제로 기록되는 것

`probe-rs download` 의 로그(`RUST_LOG=probe_rs::flashing=debug`)에 나온 elf 로드 구간은 다음과 같다.

| 구간 | 주소 | 크기 | 섹터 |
|---|---|---|---|
| `.fcb` | `0x0400_0000` | 1536 B | 0 |
| `.container` | `0x0400_1000` | 160 B | 1 |
| `.interrupts` | `0x0400_B000` | 1 KB | 11 |
| `.text` 외 | `0x0400_B400` | 10908 B | 11~13 |
| `.data` 초기값 | `0x0400_DE9C` | 96 B | 13 |

- 4 KB 섹터 **5개**만 지우고 쓴다. 나머지 플래시는 그대로다.
- `.fcb` 구간이 `0x400` 이 아니라 `0x0` 부터 1536 B 인 이유는 링커가 프로그램 헤더를 정렬하면서 앞의 OTFAD KeyBlob 자리(`0x000~0x3FF`)를 0 으로 채워 넣었기 때문이다. RM 은 이 자리를 0 으로 두라고 하므로 맞는 동작이다.
- 기록하는 동안 DTCM 의 `.data` 와 스택은 알고리즘에 덮어써진다. 끝나면 리셋하므로 문제가 없다.

| 경우 | 시간 |
|---|---|
| `probe-rs download --verify` (지우기 + 쓰기 + 비교) | **1.41 s** |
| 같은 이미지에 `--preverify` (먼저 읽어 보고 같으면 건너뜀) | **0.30 s** |

VSCode `Debug CM33` 의 `verifyBeforeFlashing` 이 이 `--preverify` 에 해당한다.
