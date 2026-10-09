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
    │   └── hw/include/         드라이버 공개 헤더 (led · uart · cli · log)
    ├── cpu/
    │   ├── cm33/               부트 코어
    │   │   ├── main.c          bspInit → hwInit → apInit → apMain
    │   │   ├── ap/             애플리케이션. 벤더 금지
    │   │   ├── bsp/            bsp.c (SysTick, delay, millis), syscalls.c
    │   │   │   ├── boot/       boot_hdr.c — FCB + 컨테이너
    │   │   │   └── ldscript/   rt1180-fw-cm33.ld
    │   │   └── hw/             hw.c (배너, boot 명령), hw_def.h (_USE_HW_*), driver/ led · uart · cli · log
    │   ├── cm7/                로드맵 25
    │   └── shared/             (로드맵 25) 코어 간 규약
    └── lib/
        ├── cmsis/              CMSIS 6 Core
        └── nxp/                SDK 에서 골라 온 것 (SOURCES.md)
```

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
| FCB | `0x0400_0400` | 512 B | `.boot_hdr.conf` |
| XMCD | `0x0400_0800` | 512 B | `.boot_hdr.xmcd_data` (비움) |
| CONTAINER | `0x0400_1000` | 8 KB | `.boot_hdr.container` |
| VECTORS | `0x0400_B000` | 1 KB | `.isr_vector` |
| FLASH | `0x0400_B400` | ~211 KB | `.text`, `.rodata`, `.module`, `.data` 초기값 |
| ITCM | `0x0FFE_0000` | 128 KB | `.ram_function` (`CodeQuickAccess`) |
| DTCM | `0x2000_0000` | 128 KB | `.data`, `.bss`, 힙 4 KB, 스택 8 KB (끝) |

FLASH 영역을 **첫 256 KB 슬롯에서 끝나게** 잡았다. 이미지가 커져서 넘치면 링크가 멈춘다. 이 자리는 나중에 부트로더가 쓴다([02-memory-map.md](02-memory-map.md) 5절).

### 컨테이너가 링커 심볼을 읽는다

`boot_hdr.c` 의 컨테이너는 이미지 위치와 크기를 상수로 적지 않는다. 링커가 계산한 절대 심볼을 쓴다.

```
__CONTAINER_IMG_OFFSET = ORIGIN(VECTORS) - ORIGIN(CONTAINER);       // 0xA000
__CONTAINER_IMG_SIZE   = __image_end - ORIGIN(VECTORS);             // 벡터 ~ 마지막 로드 바이트
__VECTOR_TABLE         = ORIGIN(VECTORS);                           // load = entry
```

`__image_end` 는 `.data` 초기값과 `.ram_function` 원본까지 포함한 끝이다. 이 크기가 작으면 ROM 이 non-XIP 로 복사할 때 잘린다. XIP 에서는 크기를 쓰지 않지만 부트로더 단계를 위해 맞춰 둔다.

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

`prj/` 에 워크스페이스가 셋 있다. 모두 같은 폴더(`../`)를 연다.

| 파일 | 숨기는 것 | IntelliSense | 디버그 구성 |
|---|---|---|---|
| `rt1180-fw-cm33.code-workspace` | `src/cpu/cm7` | `build/cm33/compile_commands.json` | **Debug CM33**, **Attach CM33** |
| `rt1180-fw-cm7.code-workspace` | `src/cpu/cm33` | `build/cm7/compile_commands.json` | (로드맵 25) |
| `rt1180-fw.code-workspace` | — | CM33 | — |

> 같은 폴더를 열기 때문에 `.vscode/launch.json` 은 세 워크스페이스가 공유하게 된다. 그래서 디버그 구성은 `.vscode` 가 아니라 **각 워크스페이스 파일의 `"launch"` 안에** 둔다.
>
> ⚠️ **태스크도 워크스페이스 파일에 둔다.** 워크스페이스 파일의 launch 는 `preLaunchTask` 를 **같은 워크스페이스 파일의 `"tasks"` 에서만** 찾는다. 처음에 태스크를 `.vscode/tasks.json` 에 두었더니 빌드(`Cmd+Shift+B`)는 되는데 디버그를 시작하면 *"Could not find the task 'build-build'"* 가 떴다. 그래서 `.vscode/tasks.json` 을 지우고 네 태스크(`build-configure`, `build-build`, `build-clean`, `flash`)를 세 워크스페이스 파일에 똑같이 넣었다. 태스크를 고칠 때는 세 파일을 같이 고친다. `.vscode/` 에는 `c_cpp_properties.json` 과 `extensions.json` 만 남는다.

디버그는 `probe-rs-debug` 타입이다. `Debug CM33` 의 동작은 다음과 같다.

1. `build-build` 태스크를 실행한다.
2. elf 를 기록한다.
3. 리셋 후 멈춘다.

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
