# 11. SDK 에서 골라 온 파일

> MCUXpresso SDK 를 설치하지 않고, 필요한 파일만 커밋을 고정해 가져오는 방법과 그 목록.
> 관련: [10-dev-environment.md](10-dev-environment.md) · [12-project-skeleton.md](12-project-skeleton.md)
>
> 출처: `firmware/rt1180-fw/tools/fetch_nxp_sdk.py`, `firmware/rt1180-fw/src/lib/nxp/SOURCES.md`

---

## 1. 왜 이렇게 하나

MCUXpresso SDK 는 west 매니페스트로 수십 개 저장소를 묶은 구조다. 전부 받으면 GB 단위다. 우리가 실제로 쓰는 것은 다음 세 가지뿐이다.

- 디바이스 레지스터 헤더
- startup / SystemInit
- 드라이버 몇 개

그래서 `tools/fetch_nxp_sdk.py` 하나로 해결한다.

- **커밋 SHA 를 고정**한다. `main` 을 쓰면 같은 명령이 날마다 다른 결과를 낸다.
- 받은 파일은 **저장소에 커밋**한다. 평소 빌드에 네트워크가 필요 없다.
- `--check` 로 받은 파일이 원본과 같은지 확인한다. 다른 PC 에서 같은 환경인지 검증하는 데 쓴다.
- 결과 목록과 sha256 은 `src/lib/nxp/SOURCES.md` 에 남는다.

```bash
cd firmware/rt1180-fw
python3 tools/fetch_nxp_sdk.py --check     # 148개 중 0개 다름
```

## 2. 고정한 버전

| 저장소 | ref | 내용 |
|---|---|---|
| `nxp-mcuxpresso/mcux-devices-rt` | `f7b174e` (2026-08-27) | 디바이스 헤더, startup, SoC 드라이버 |
| `nxp-mcuxpresso/mcuxsdk-core` | `c6f4223` (2026-09-03) | 공통 드라이버 |
| `nxp-mcuxpresso/mcuxsdk-examples` | `3151bc0` (2026-09-21) | FRDM 보드 파일 |
| `ARM-software/CMSIS_6` | `v6.4.0` | CMSIS Core |

## 3. 무엇을 어디로

| 저장 위치 (`src/lib/`) | 원본 | 용도 |
|---|---|---|
| `cmsis/Core/Include/` | CMSIS_6 `CMSIS/Core/Include` (M-profile 만) | `core_cm33.h`, `core_cm7.h` |
| `nxp/devices/MIMXRT1186/` | `RT1180/MIMXRT1186/` | `MIMXRT1186_cm33.h` 등 디바이스 헤더, `system_*.c`, `gcc/startup_*.S` |
| `nxp/devices/MIMXRT1186/gcc/*.ld` | 〃 | **참고용**. 빌드는 `bsp/ldscript` 의 우리 스크립트를 쓴다 |
| `nxp/devices/periph/` | `RT1180/periph/*.h` (107개) | 주변장치 레지스터 정의. 디바이스 헤더가 전부 include 한다 |
| `nxp/drivers/` | `RT1180/MIMXRT1189/drivers/` | `fsl_clock`, `fsl_pmu`, `fsl_iomuxc.h` (RT1186 은 RT1189 것을 공유한다) |
| `nxp/drivers/` | `mcuxsdk-core/drivers/` | `fsl_common`, `fsl_common_arm`, `fsl_rgpio` |
| `nxp/boards/frdmimxrt1186/` | `_boards/frdmimxrt1186/xip/` | FCB 타입 정의 헤더. `.c.ref` 는 값 참고용 (빌드 제외) |

## 4. 디바이스 헤더 구조

새 SDK 는 레지스터 정의를 주변장치별 파일로 쪼갰다.

```
fsl_device_registers.h          CPU_MIMXRT1186CVJ8C_cm33 정의로 분기
└─ MIMXRT1186_cm33.h            PERI_*.h 전부 include
   ├─ MIMXRT1186_cm33_COMMON.h  IRQ 번호, 베이스 주소 (S / NS), core_cm33.h
   └─ PERI_RGPIO.h, PERI_CCM.h, …
```

`MIMXRT1186_cm33_COMMON.h` 는 `__ARM_FEATURE_CMSE` 에 따라 Secure 베이스(`0x5xxx_xxxx`)나 NS 베이스를 고른다. 우리는 `-mcmse` 로 빌드하므로 Secure 베이스가 선택된다([02-memory-map.md](02-memory-map.md) 2절).

## 5. 드라이버를 추가할 때

1. `tools/fetch_nxp_sdk.py` 의 `FILES` 에 원본 경로를 더한다.
2. `python3 tools/fetch_nxp_sdk.py` 를 실행한다.
3. 최상위 `CMakeLists.txt` 의 `NXP_SRC_FILES` 에 `.c` 를 더한다. **glob 하지 않는다.**
4. 빌드가 `fsl_xxx.h: No such file` 로 깨지면 그 헤더의 원본을 찾아 1번부터 다시 한다.

> 예: `fsl_clock.c` 는 `fsl_pmu.h` 를 include 한다. 처음 빌드에서 이것 때문에 깨져서 `fsl_pmu.[ch]` 를 추가했다.

SDK 파일은 **고치지 않는다.** 고쳐야 할 일이 생기면 우리 코드에서 감싸거나 덮어쓴다. 예를 들어 `SystemCoreClock` 은 `bsp.c` 에서 고친다.
