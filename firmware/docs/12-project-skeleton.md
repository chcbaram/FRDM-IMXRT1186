# 12. 프로젝트 구조와 빌드

> `firmware/rt1180-fw` 의 디렉터리, 계층 규칙, 빌드 타깃, 링크 배치. titan-mini(RA8P1 듀얼코어)의 구조를 그대로 따르고 NXP 에 맞게 바꾼 부분만 적는다.
> 관련: [11-sdk-vendoring.md](11-sdk-vendoring.md) · [02-memory-map.md](02-memory-map.md) · [20-led.md](20-led.md)

---

## 1. 디렉터리

```
firmware/rt1180-fw/
├── CMakeLists.txt              공유 경로, SDK 드라이버 목록, 코어 서브디렉터리
├── .clang-format  .gitignore
├── .vscode/                    c_cpp_properties · extensions (코어 공통)
├── prj/                        코어별 VSCode 워크스페이스 (태스크 · 디버그 구성 포함)
├── tools/
│   ├── arm-none-eabi-gcc.cmake 툴체인 (titan-mini 와 같다)
│   ├── flash.cmake             probe-rs 기록 타깃
│   ├── fetch_nxp_sdk.py        SDK 파일 가져오기 (11장)
│   ├── split_compdb.py         코어별 compile_commands.json
│   ├── check_layers.py         계층 규칙 검사
│   └── svd/                    (저장소 밖) setup_tools.py 가 채운다
└── src/
    ├── common/                 포터블 코드. 벤더 금지
    │   ├── def.h  err_code.h  evt_code.h
    │   ├── core/               qbuffer, util_core
    │   └── hw/
    │       ├── include/        드라이버 공개 헤더 (led · pwm · uart · cli · log · ipc · cmd …)
    │       └── src/            두 코어가 같이 쓰는 포터블 구현 (swtimer, cmd 패킷)
    ├── cpu/
    │   ├── cm33/               부트 코어
    │   │   ├── main.c          bspInit → hwInit → apInit → apMain
    │   │   ├── ap/             애플리케이션. 벤더 금지
    │   │   │   └── modules/    module.c (MODULE_DEF) · common/cli · common/cmd (cmd_task.c · driver/drv_ipc.c · process/cmd_peer.c)
    │   │   ├── bsp/            bsp.c (delay, millis, micros), mpu.c, clock.c, syscalls.c
    │   │   │   ├── rtos/       FreeRTOSConfig.h, rtos.c (훅) — docs/28
    │   │   │   └── ldscript/   rt1180-fw-cm33.ld
    │   │   └── hw/             hw.c (초기화 순서, 배너), hw_def.h (_USE_HW_*), driver/ led · pwm · uart · cli · log · event · qspi · psram · ipc · hw_info · osal/thread …
    │   ├── cm7/                CM7 (로드맵 25). 같은 ap/bsp/hw 구조 — modules/common/cmd, osal/thread, log (버퍼만)
    │   └── shared/             코어 간 규약 shared.h — 이미지 헤더, 공유 블록, 메시지 id, 링, cmd 번호
    └── lib/
        ├── cmsis/              CMSIS 6 Core
        ├── freertos/           FreeRTOS-Kernel V11.1.0 (커널 · CM33 NTZ 포트 · heap_4)
        └── nxp/                SDK 에서 골라 온 것 (SOURCES.md)
```

부트로더는 옆 폴더 `firmware/rt1180-boot` 다([41](41-bootloader.md) 2절). main · ap · bsp · hw 만 가지고, 공통 헤더 · SDK · CM33 드라이버는 위 `src/` 를 CMake 경로로 가져온다.

### 계층 규칙 — ap 는 벤더 HAL 을 모른다

| 층 | NXP SDK / 레지스터 |
|---|---|
| `ap/`, `common/`, `cpu/shared/` | **금지.** `hw/include` 의 공용 API 만 쓴다 |
| `hw/driver/`, `bsp/`, `hw/hw*.c/h`, `main.c` | 허용 |

`tools/check_layers.py` 가 이 규칙을 검사한다. titan-mini 의 검사기를 NXP 패턴으로 바꿨다. 검사 대상은 `fsl_*.h`, `PERI_*.h`, `RGPIO*`, `IOMUXC*`, `CLOCK_*`, `kCLOCK_*`, `status_t` 등이다.

```bash
python3 tools/check_layers.py     # 벤더 HAL 금지 층 14개 파일 검사, 위반 0건
```

### 핀 번호는 드라이버 `.c` 에 적는다

titan-mini 와 같다. `hw_def.h` 에는 `_USE_HW_LED` 와 `HW_LED_MAX_CH` 만 둔다. 핀, 경로, 극성은 `hw/driver/led.c` 맨 위 주석과 테이블에 적는다.

## 2. 빌드 타깃

| 명령 | 결과 |
|---|---|
| `cmake --build build` | `build/cm33/rt1180-fw-cm33.{elf,bin,map}` + 코어별 compile_commands |
| `cmake --build build --target flash` | probe-rs 로 elf 기록 (`--verify`) 후 리셋 |
| `-DBUILD_CM7=ON` | 로드맵 25 전까지는 일부러 실패한다 |

- 빌드 트리는 하나다(titan-mini 와 같은 이유). 통합 이미지와 동시 기록을 위해서다.
- 같은 SDK 소스가 코어마다 다른 `-mcpu` 로 컴파일되므로, `compile_commands.json` 은 `split_compdb.py` 가 코어별로 갈라 둔다.

### 컴파일 옵션 (CM33)

| 옵션 | 이유 |
|---|---|
| `-mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard` | CM33 은 단정도 FPU |
| `-mcmse` | Secure 로 실행한다. 디바이스 헤더가 Secure 레지스터 베이스를 고른다 |
| `CPU_MIMXRT1186CVJ8C_cm33` | `fsl_device_registers.h` 의 분기 |
| `__STARTUP_CLEAR_BSS`, `__STARTUP_INITIALIZE_RAMFUNCTION` | SDK startup 의 옵션. `.bss` 클리어와 ITCM 코드 복사 |
| SDK 소스만 `-Os -w` | 프로젝트 코드의 경고를 가리지 않게 |

## 3. 링크 배치

링커 스크립트는 우리가 소유한다(`bsp/ldscript/rt1180-fw-cm33.ld`). SDK 스크립트는 참고용으로만 받아 두었다.

| 영역 | 주소 | 크기 | 섹션 |
|---|---|---|---|
| VECTORS | `0x0404_1000` | 1 KB | `.isr_vector` |
| FLASH | `0x0404_1400` | ~7.7 MB | `.version`(`firm_ver_t`, 맨 앞), `.text`, `.rodata`, `.module`, `.data` 초기값 |
| ITCM | `0x0FFE_0000` | 128 KB | `.ram_function` (`CodeQuickAccess`) |
| DTCM | `0x2000_0000` | 128 KB | `.data`, `.bss`, 힙 4 KB, 스택 8 KB (끝) |

FLASH 영역은 CM7 슬롯(`0x0480_0000`) 앞에서 끝난다. 넘치면 링크가 멈춘다. 앞의 `0x0400_0000` 부트 슬롯(FCB · XMCD · 컨테이너 · 부트로더)과 `0x0404_0000` TAG 는 이 이미지에 없다. 부트 헤더는 부트로더 `rt1180-boot` 가 가지고([41](41-bootloader.md)), TAG 는 빌드 뒤 `tools/mkfwtag.py` 가 bin 앞에 붙인다([42](42-app-jump.md)).

로드맵 41 전까지는 이 이미지가 부트 헤더를 가지고 `0x0400_B000` 에서 돌았다. 그때 쓰던 `boot_hdr.c` 와 컨테이너 링커 심볼은 부트로더로 옮겼다.

## 4. 크로스 플랫폼 규칙

titan-mini 의 규칙을 그대로 지킨다. 실제로 돌려 본 것은 macOS 뿐이므로 **정적 규칙으로 담보한다.**

| 규칙 | 이유 |
|---|---|
| CMake 경로는 `${CMAKE_CURRENT_SOURCE_DIR}` 기준, 구분자는 `/` | 절대 경로는 다른 기계에서 깨진다 |
| 제너레이터는 **Ninja** | OS 별 태스크 분기가 필요 없다 |
| `.sh` / `.bat` 를 만들지 않는다 | 기록은 CMake 타깃, 보조 작업은 Python (표준 라이브러리만) |
| `tasks.json` 은 `"type": "process"` + 인자 배열 | 셸 문법 차이를 피한다 |
| 툴체인은 `ARM_TOOLCHAIN_DIR` 환경변수가 1순위, 없으면 PATH | 경로 하드코딩을 피한다 |
| `.gitattributes` 에 `text=auto eol=lf` | 링커 스크립트와 `.S` 의 CRLF 를 막는다 |
| SDK 파일은 SHA 고정 + `--check` | PC 마다 다른 SDK 가 섞이지 않는다 |

## 5. VSCode

**디버그 구성과 태스크는 `.vscode/launch.json` · `.vscode/tasks.json` 에 둔다.** 폴더(`rt1180-fw`, `rt1180-boot`)를 그대로 열어도, `prj/` 의 워크스페이스로 열어도 같은 구성이 보인다. 워크스페이스 파일에는 폴더 설정(숨기는 폴더 · IntelliSense)만 남겼다.

| `prj/` 워크스페이스 | 숨기는 것 | IntelliSense |
|---|---|---|
| `rt1180-fw-cm33.code-workspace` | `src/cpu/cm7` | `build/cm33/compile_commands.json` |
| `rt1180-fw-cm7.code-workspace` | `src/cpu/cm33` | `build/cm7/compile_commands.json` |
| `rt1180-fw.code-workspace` | — | CM33 |

| 디버그 구성 (`rt1180-fw/.vscode/launch.json`) | 방식 |
|---|---|
| **Flash + Debug CM33 (Cortex-Debug)** · **Attach CM33 (Cortex-Debug)** | GDB + probe-rs GDB 서버 ([42](42-app-jump.md) 5절). `main` 에서 멈춘다 |
| **Flash + Attach CM33** · **Attach CM33** | probe-rs 확장 |
| **Flash + Attach CM7** · **Attach CM7** | probe-rs 확장 ([14](14-cm7-debug.md)) |
| **Flash + Attach CM33 + CM7** · **Attach CM33 + CM7** | probe-rs 확장, 두 코어 |

부트로더(`rt1180-boot/.vscode`)도 같은 구성이다: Cortex-Debug 두 개 + probe-rs `Debug CM33` · `Attach CM33`.

| 태스크 | 내용 |
|---|---|
| `build-configure` · `build-build` · `build-clean` | CMake. 앱은 configure 가 `-DBUILD_CM7=ON` 으로 돈다 |
| `flash-all` · `flash-cm33` · `flash-cm7` | probe-rs 기록. CMake 타깃 `flash`(CM33 + CM7) · `flash-cm33` · `flash-cm7`. 부트로더 쪽은 `flash-boot` |
| `download-all` · `download-cm33` · `download-cm7` | UART 로 앱 · CM7 이미지 쓰기 (`rt1180-boot/tools/download.py`, [43](43-uart-download.md)). 포트는 Firmware Task Manager 확장의 시리얼 포트 선택으로 고르고, 마지막 값을 기억한다. 앱 쪽은 빌드한 뒤 보낸다 |
| `download-list` | 시리얼 포트 목록 (`*` = `auto` 가 고르는 것) |
| `gdb-server` | `tools/gdbserver.py start` — `probe-rs gdb` 를 떼어 낸 프로세스로 띄우고 `Firing up GDB stub` 이 나오면 끝난다. 남아 있던 서버는 먼저 내린다 |
| `flash + gdb-server` | 기록한 뒤 서버. Cortex-Debug 의 `preLaunchTask` |
| `gdb-server-stop` | `tools/gdbserver.py stop` (`build/probe-rs-gdb.pid`). Cortex-Debug 의 `postDebugTask` |

> 처음에는 구성을 워크스페이스 파일에 두었다. 같은 폴더를 여는 워크스페이스 셋이 서로 다른 구성을 가지게 하려던 것이다. 이때 launch 는 워크스페이스에, 태스크는 `.vscode/tasks.json` 에 두었더니 *"Could not find the task 'build-build'"* 가 떴다(launch 와 태스크는 같은 곳에 있어야 한다). 그 뒤 폴더만 열면 구성이 보이지 않는 불편이 커서 둘 다 `.vscode` 로 옮겼다.

디버그 타입은 둘이다. 로드맵 42 부터 앱은 부트로더가 점프해 들어오므로 probe-rs 구성은 **모두 attach** 다. `Flash + Attach` 는 `flash-all` 태스크(TAG 를 붙인 bin · CM7 기록 → 리셋) 뒤에 붙는다. elf 를 디버거가 직접 쓰면 TAG 가 옛 것으로 남아 부트로더가 앱을 실행하지 않기 때문이다. Cortex-Debug 구성도 GDB 로 elf 를 쓰지 않고(`overrideLaunchCommands`), `flash-all`(부트로더는 `flash-boot`) 태스크로 쓴 뒤 `monitor reset halt` 부터 시작한다([42](42-app-jump.md) 5절).

SVD 는 `tools/svd/MIMXRT1186_cm33.xml` 이다(`setup_tools.py` 가 준비).

## 6. 현재 빌드 결과

```
Memory region         Used Size  Region Size  %age Used
             FCB:         512 B        512 B    100.00%
            XMCD:           0 B        512 B      0.00%
       CONTAINER:         160 B         8 KB      1.95%
         VECTORS:          1 KB         1 KB    100.00%
           FLASH:       10908 B       211 KB      5.05%
            ITCM:           0 B       128 KB      0.00%
            DTCM:       12752 B       128 KB      9.73%
```

DTCM 사용량 대부분은 스택(8 KB)과 힙(4 KB)이다.
