# 20. LED 점멸 — 1차 목표

> 아무것도 없는 상태에서 QSPI 부팅으로 녹색 LED 를 500 ms 마다 토글하기까지의 기록. 무엇을 왜 그렇게 했는지, 어디서 막혔는지, 어떻게 확인했는지를 적는다.
> 관련: [01-boot-sequence.md](01-boot-sequence.md) · [03-board-mapping.md](03-board-mapping.md) · [12-project-skeleton.md](12-project-skeleton.md)

---

## 1. 목표와 범위

| 항목 | 내용 |
|---|---|
| 목표 | J60=`100` 으로 전원을 넣으면 D4 녹색이 500 ms 마다 토글 |
| 코어 | CM33 만 |
| 클럭 | 손대지 않는다 — ROM 이 남긴 200 MHz 그대로 |
| 부팅 | QSPI XIP, 서명 없는 컨테이너 |
| 제외 | UART, PLL, 캐시/MPU, CM7 |

## 2. 보드에서 먼저 확인한 것

처음 쓰는 칩이라 코드를 쓰기 전에 **공장 데모 이미지와 퓨즈를 디버거로 읽어** 문서의 가정을 확인했다. 코어를 멈추지 않고 읽는다.

```bash
$ probe-rs read --chip MIMXRT1180 b32 0x04000400 4      # FCB
04000400: 42464346 56010400 00000000 00030301           # "FCFB", V1.4.0
$ probe-rs read --chip MIMXRT1180 b32 0x04001000 12     # 컨테이너
04001000: 8700a000 00000000 01010000 00000090 0000a000 00004a00 0400b000 00000000
04001020: 0400b000 00000000 00000013 00000000
$ probe-rs read --chip MIMXRT1180 b32 0x0400B000 2      # 벡터 테이블
0400b000: 20020000 0400f1d1
```

| 읽은 값 | 의미 |
|---|---|
| `8700a000` | tag `0x87`, 길이 `0xA0`, version 0 |
| `00000090` | 서명 블록 오프셋 `0x90` = 헤더 16 + 엔트리 128 |
| `0000a000` / `0400b000` | 이미지 오프셋 `0xA000`, load = entry = `0x0400_B000` |
| `00000013` | 플래그 — 실행 이미지, CM33, SHA256 |
| `20020000` | 초기 SP = DTCM 끝 |

[01-boot-sequence.md](01-boot-sequence.md) 4절의 배치가 그대로 맞았다. 퓨즈(`XSPI_INSTANCE=1`, `BOOT_FREQ=0`)도 이때 읽었다(01장 2절).

**첫 기록 전에 공장 이미지 앞 64 KB 를 백업**했다(`hardware/ref/factory/factory_64k.bin`). 데모는 `0x0400_FA00` 에서 끝난다.

## 3. 부트 헤더 — `bsp/boot/boot_hdr.c`

| 구조체 | 섹션 | 내용 |
|---|---|---|
| `boot_fcb` | `.boot_hdr.conf` → `0x0400_0400` | SDK 와 같은 값. W25Q128 Quad I/O `0xEB`, 100 MHz, DQS 루프백 |
| `boot_container` | `.boot_hdr.container` → `0x0400_1000` | RM Table 82~86 을 보고 직접 정의. 이미지 1개, 서명 없음 |

컨테이너의 이미지 위치와 크기는 링커 심볼(`__CONTAINER_IMG_OFFSET`, `__CONTAINER_IMG_SIZE`, `__VECTOR_TABLE`)로 채운다. 구조체 크기는 `_Static_assert` 로 묶어 두었다(헤더 0x10, 엔트리 0x80, 서명 블록 0x10).

빌드 결과를 공장 이미지와 비교하면 다음과 같다.

| 필드 | 공장 데모 | 우리 이미지 |
|---|---|---|
| tag / 길이 | `0x87` / `0xA0` | `0x87` / `0xA0` |
| 서명 블록 오프셋 | `0x90` | `0x90` |
| fuse version | 1 | 0 |
| 이미지 오프셋 | `0xA000` | `0xA000` |
| 크기 | `0x4A00` | `0x2F48` |
| load / entry | `0x0400_B000` | `0x0400_B000` |
| 플래그 | `0x13` (SHA256) | `0x213` (SHA512, SDK 기본) |

## 4. 드라이버 — `hw/driver/led.c`

```c
static const led_tbl_t led_tbl[LED_MAX_CH] =
{
  {RGPIO2, 9,  {IOMUXC_GPIO_EMC_B1_09_GPIO2_IO09}, _DEF_HIGH, _DEF_LOW},   // RED
  {RGPIO2, 11, {IOMUXC_GPIO_EMC_B1_11_GPIO2_IO11}, _DEF_HIGH, _DEF_LOW},   // GREEN
  {RGPIO3, 7,  {IOMUXC_GPIO_EMC_B1_39_GPIO3_IO07}, _DEF_HIGH, _DEF_LOW},   // BLUE
};
```

초기화 순서는 SDK `BOARD_InitLEDsPins()` 와 같다.

1. `CLOCK_EnableClock(kCLOCK_Iomuxc1)`. IOMUXC 의 LPCG 를 켜야 핀먹스 레지스터가 먹는다.
2. `RGPIO_PinInit()`. 출력 방향과 초기 레벨(꺼짐)을 정한다.
3. `IOMUXC_SetPinMux(..., ALT5)`. 패드를 GPIO 기능에 연결한다.

`IOMUXC_GPIO_xxx` 매크로는 다섯 값(mux 레지스터, 모드, input 레지스터, daisy, config 레지스터)으로 펼쳐진다. 그래서 테이블에 배열로 담아 둔다.

SDK 예제의 `BOARD_CommonSetting()` 은 TRDC 접근 권한을 연다. **CM33 Secure 에서 GPIO 만 쓰는 데는 필요 없었다.** DMA 나 CM7 이 들어오는 단계에서 다시 본다.

## 5. bsp — 클럭을 읽기만 한다

```c
SystemCoreClock = CLOCK_GetRootClockFreq(kCLOCK_Root_M33);
SysTick_Config(SystemCoreClock / 1000);
```

`system_MIMXRT1186_cm33.c` 는 `SystemCoreClock` 을 **240 MHz** 로 초기화한다. 이 값은 `BOOT_FREQ=1` 일 때의 클럭이다. 이 보드는 `BOOT_FREQ=0` 이라 실제는 200 MHz 다. 그대로 쓰면 SysTick 이 20% 느려진다. 그래서 CCM 의 M33 클럭 루트를 실제로 읽어 고친다. SDK 파일은 고치지 않는다.

## 6. 빌드 · 기록

```bash
cd firmware/rt1180-fw
cmake -S . -B build -G Ninja
cmake --build build
cmake --build build --target flash      # probe-rs download --verify → reset
```

```
             FCB:         512 B        512 B    100.00%
       CONTAINER:         160 B         8 KB      1.95%
         VECTORS:          1 KB         1 KB    100.00%
           FLASH:       10908 B       211 KB      5.05%
            DTCM:       12752 B       128 KB      9.73%
```

## 7. 검증 — 눈으로 보지 않고 확인하는 법

모두 `probe-rs read` 로 실행 중에 읽었다.

| 확인 | 명령 / 결과 |
|---|---|
| 플래시에 쓴 컨테이너 | `0x04001000: 8700a000 00000000 01000000 00000090 0000a000 00002f48 0400b000` — elf 와 같다 |
| RGPIO2 방향 | `PDDR (0x5381_0054) = 0x00000A00`. 핀 9(빨강), 11(녹색)이 출력이다 |
| RGPIO2 출력이 토글 | `PDOR (0x5381_0040)` 을 0.35 초 간격으로 읽으면 `0x800 → 0 → 0x800 → 0x800 → 0`. bit 11 만 움직인다 |
| 코어 클럭 | `SystemCoreClock (0x2000_005C) = 0x0BEBC200` = **200,000,000** |
| 1 ms 틱 | `systick_ms` 를 5 초 간격으로 두 번 읽으면 5065 증가. 차이는 프로브 지연이다 |
| 재현성 | 빈 디렉터리에서 다시 빌드한 `.bin` 이 같다 |

> 처음엔 `0x5381_0000` 을 읽고 값이 안 바뀐다고 착각했다. 그 주소는 `VERID` 다. RGPIO 레지스터 배치는 `PERI_RGPIO.h` 의 구조체 주석에 오프셋이 적혀 있다. `PDOR` 은 `+0x40` 이다.

## 8. 걸렸던 것들

### `fsl_clock.c` 가 `fsl_pmu.h` 를 찾는다

`CLOCK_GetRootClockFreq` 하나 쓰려고 `fsl_clock.c` 를 넣었는데, PLL 함수들이 `PMU_StaticEnablePllLdo()` 를 부른다. `fsl_pmu.[ch]` 를 fetch 목록과 `NXP_SRC_FILES` 에 추가했다([11-sdk-vendoring.md](11-sdk-vendoring.md) 5절).

### newlib nosys 경고

```
warning: _write is not implemented and will always fail
```

SDK 드라이버의 `assert()` → `__assert_func` → `fiprintf` 가 stdio 를 끌어온다. 그러면 nosys.specs 의 빈 스텁(`_write`, `_read`, `_close`, `_fstat`, `_isatty`, `_lseek`, `_getpid`, `_kill`)이 링크되고, ld 가 하나마다 경고를 낸다. 동작에는 문제가 없지만 진짜 경고가 묻힌다.

`bsp/syscalls.c` 에 이 함수들을 직접 정의해 없앴다. ST 템플릿(N657X0 의 `syscalls.c`)은 라이선스 때문에 가져오지 않고 최소 스텁만 썼다. `_write` 는 지금 출력을 버리고 `weak` 로 두었다. UART 단계(로드맵 21)에서 덮어써 콘솔로 보낸다.

### `SystemCoreClock` 이 240 MHz 로 되어 있다

5절에 적었다. 퓨즈를 읽지 않았다면 SysTick 주기가 왜 1.2 ms 인지 한참 헤맸을 것이다.

## 9. 다음

- [ ] 21 — LPUART1(VCOM) + CLI + 부팅 배너, `_write` → UART
- [ ] 22 — PLL: CM33 300 MHz (여기서 `SystemCoreClock` 을 CCM 에서 읽는 방식이 그대로 쓰인다)
- [ ] VSCode `Debug CM33` 구성 실제 실행 확인
