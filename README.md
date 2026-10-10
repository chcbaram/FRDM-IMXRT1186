# FRDM-IMXRT1186

NXP FRDM-IMXRT1186 보드(i.MX RT1186 — Cortex-M33 + Cortex-M7, EdgeLock, TSN 스위치, EtherCAT)의 베어메탈 펌웨어.

제조사 IDE 와 SDK 설치 없이 **gcc + CMake + Ninja + probe-rs** 만으로 빌드하고, 기록하고, 디버그한다.

| | |
|---|---|
| MCU | MIMXRT1186CVJ8C (CM33 부트 코어 240 MHz 사용 · 최대 300 / CM7 800 MHz) |
| 부트 플래시 | W25Q128JV 16 MB QSPI (FlexSPI2) |
| 디버거 | 온보드 MCU-Link (CMSIS-DAP) → probe-rs |
| 툴체인 | Arm GNU Toolchain 15.3 · CMake 4.4 · Ninja 1.13 |
| SDK | MCUXpresso SDK 에서 필요한 파일만 (커밋 SHA 고정, BSD-3) |
| 상태 | CM33 240 MHz · LED · UART 콘솔(MCU-Link VCOM) · CLI · 로그 · 버튼 · RTC · reset |

## 구성

```
hardware/           회로도 · UM · BOM · 레이아웃  (ref/ 는 저장소 밖: RM, 데이터시트)
tools/
  setup_tools.py    새 PC 점검 + SVD 준비
firmware/
  docs/             부팅 · 메모리 맵 · 보드 · 환경 · 구현 기록 (한국어)
  rt1180-fw/        펌웨어 (CMake, 코어별 src/cpu/cm33, cm7)
```

## 준비

```bash
python3 tools/setup_tools.py
```

OS 별 설치 절차는 [firmware/docs/13-os-setup.md](firmware/docs/13-os-setup.md) 에 있다.

## 빌드

```bash
cd firmware/rt1180-fw
cmake -S . -B build -G Ninja
cmake --build build
cmake --build build --target flash
```

보드는 J60 = `100`(QSPI 부팅)에 USB 를 J23 에 꽂는다.

## 문서

[firmware/docs/README.md](firmware/docs/README.md) 에서 시작한다. 현재 상태, 로드맵, 목차가 있다.

| 대역 | 내용 |
|---|---|
| 00~09 | 부팅 시퀀스 · 메모리 맵 · 보드 결선 · 네트워크 개요 |
| 10~19 | 개발 환경 · SDK 가져오기 · 프로젝트 구조 · OS 별 구축 |
| 20~ | 기능별 구현 기록 |

## 검사

```bash
python3 firmware/docs/check_svg.py
python3 firmware/docs/check_links.py
python3 firmware/rt1180-fw/tools/check_layers.py
python3 firmware/rt1180-fw/tools/fetch_nxp_sdk.py --check
```

## 라이선스

[LICENSE](LICENSE). `firmware/rt1180-fw/src/lib/` 아래는 각 원본의 라이선스(NXP BSD-3-Clause, Arm Apache-2.0)를 따른다.
