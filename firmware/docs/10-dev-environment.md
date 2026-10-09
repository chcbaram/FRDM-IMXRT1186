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
| J24, J26 | 단락 (기본) — SW2 와 디버거 리셋이 POR 로 이어진다 |

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
