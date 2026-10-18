# 28. FreeRTOS — CM33 (+ thread · module)

> CM33 에 FreeRTOS 를 올리고, titan-mini 의 `thread`(스레드 등록 · `thread info/cpu`)와 N657X0 의 `module`(MODULE_DEF 자기 등록)을 들였다. CM7 은 아직 bare-metal 이다 — 네트워크(50 번대)를 CM7 에 둘 계획이라 다음에 CM7 도 올린다.
> 관련: [25-cm7-boot.md](25-cm7-boot.md) 10 · 11절 (doorbell, 패킷) · [26-qspi-nor.md](26-qspi-nor.md) 6절 (인터럽트 정지) · [12-project-skeleton.md](12-project-skeleton.md)
>
> 출처: FreeRTOS-Kernel `V11.1.0` (`portable/GCC/ARM_CM33_NTZ`), titan-mini `bsp/rtos` · `osal/thread`, NUCLEO-N657X0 `ap/modules/module.c`.

---

## 1. 결과

```
[OK] moduleInit()
     count : 2
     cmd              OK
     cli              OK
[  ] Thread Started : cli
cli# thread cpu
name              pri     cpu
cli                 3    0.0 %
main                2    0.5 %
IDLE                0   99.4 %
Tmr Svc             7    0.0 %
cli# module info
idx  name             pri  update init
0    cmd              7    yes    OK
1    cli              6    -      OK
```

기존 기능은 그대로다: 모든 드라이버 `[OK]`, CM7 기동, `ipc ping 1000` · `peer echo 512 1000` 1000/1000, `qspi test`, `psram test`. 왕복 시간은 조금 늘었다(ping 4 → 5 µs, echo 537 → 577 µs). 틱 인터럽트와 문맥 전환 몫이다.

## 2. 구조 — titan-mini 와 같다

```
main()
  bspInit()                     클럭 · MPU · 캐시. SysTick 은 켜지 않는다
  xTaskCreate(mainThread)       main 스레드 하나만 (_HW_DEF_THREAD_MAIN_PRI 2, 4 KB)
  vTaskStartScheduler()
    mainThread
      hwInit()                  드라이버. threadInit() 은 cliInit/logInit 바로 뒤
      apInit()                  moduleInit()  ← 모듈 init 이 스레드를 등록한다 (cli)
      apMain()                  threadBegin()  → 루프: LED · moduleUpdate · delay(1)
    cli 스레드 (pri 3, 4 KB)    cliMain · delay(2)
```

- `hwInit` 부터 모두 스케줄러 아래에서 돈다. 그래서 `millis()` · `delay()` 가 처음부터 RTOS 틱을 쓴다.
- `apMain()` 루프 끝의 `delay(1)` 이 없으면 main 스레드가 CPU 를 혼자 잡아 IDLE 이 돌지 못한다.

| 파일 | 내용 |
|---|---|
| `cm33/bsp/rtos/FreeRTOSConfig.h` | 설정 (3절) |
| `cm33/bsp/rtos/rtos.{h,c}` | `rtosIsRunning()`, 훅(틱 · 정적 메모리 · 스택 넘침 · malloc 실패 · assert). `bsp.h` 가 무조건 include 한다 |
| `cm33/bsp/bsp.c` | `delay()` = `vTaskDelay` (스케줄러 전에는 DWT 루프), `millis()` = 틱, `micros()` = 틱 + SysTick 보간, 런타임 카운터 |
| `cm33/main.c` | 위 순서 |
| `common/hw/include/osal/thread.h` · `cm33/hw/driver/osal/thread.c` | titan-mini 그대로. 등록 → `threadBegin()` 에서 생성, CLI `thread info` · `thread cpu` |
| `cm33/ap/modules/module.{h,c}` | N657X0 그대로. `MODULE_DEF` 디스크립터를 `.module` 섹션에 모아 우선순위 순으로 `init()`, 등록 순으로 `update()`. CLI `module info` |
| `cm33/ap/modules/common/cli/cli.c` | titan-mini 그대로. `MODULE_DEF(cli)` 의 `init()` 이 `cliOpen()` 후 `threadCreate("cli")` |

### 모듈

| 모듈 | 우선순위 | 방식 | 하는 일 |
|---|---|---|---|
| `cmd` | LOWEST | `update()` (main 스레드) | 코어 간 cmd 채널 수신 ([25](25-cm7-boot.md) 11절) |
| `cli` | NORMAL | 자기 스레드 (pri 3) | `cliMain()`. 명령이 오래 걸려도 main 스레드와 LED 는 계속 돈다 |

**cmd 채널 잠금.** CLI 가 스레드로 나가면서, CLI 의 `peer` 명령(보내고 응답을 기다린다)과 main 스레드의 cmd 모듈 수신이 같은 링 버퍼를 동시에 읽을 수 있게 됐다. 그러면 응답을 가로챈다. `cmd_task` 에 뮤텍스(`cmdTaskLock` / `cmdTaskUnlock`)를 두었다. 백그라운드 수신은 바로 잠글 수 있을 때만 읽고, `peer` 명령은 주고받는 동안 잠금을 쥔다.

## 3. 설정에서 짚을 것 (`FreeRTOSConfig.h`)

| 항목 | 값 | 이유 |
|---|---|---|
| 포트 | `ARM_CM33_NTZ` | TrustZone 을 쓰지 않는다 |
| `configRUN_FREERTOS_SECURE_ONLY` | **1** | 이 칩은 Secure 로만 돈다(SAU 꺼짐). port.c 머리말: *"secure only = SECURE_ONLY 1 + TRUSTZONE 0"*. 틀리면 EXC_RETURN 이 Non-secure 로 잡혀 첫 태스크에서 fault |
| `configENABLE_TRUSTZONE` · `MPU` · `FPU` | 0 · 0 · 1 | MPU 는 `bsp/mpu.c` 가 쓴다 |
| `configPRIO_BITS` | 3 | NVIC 우선순위 0~7 (`__NVIC_PRIO_BITS`) |
| `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` | 2 | 우선순위 값 2 이상인 ISR 만 `FromISR` API 를 쓴다. UART · MU 는 5 |
| `configTICK_RATE_HZ` | 1000 | 틱 = 1 ms = `millis()` |
| `configUSE_TICKLESS_IDLE` | 0 | `micros()` 가 SysTick LOAD 를 믿는다 |
| `configTOTAL_HEAP_SIZE` | 32 KB (heap_4, System TCM) | 지금 28 KB 남는다 |
| `configUSE_TICK_HOOK` | 1 | swtimer 1 ms 를 여기서 (4절) |
| `configGENERATE_RUN_TIME_STATS` | 1 | DWT 사이클 >> 8 (약 0.94 MHz) |

## 4. 바꾼 것 / 걸렸던 것

- **SysTick 은 포트가 가진다.** ARMv8-M 포트는 `SysTick_Handler` · `SVC_Handler` · `PendSV_Handler` 를 이름 그대로 정의한다. `bsp.c` 의 SysTick 설정과 핸들러는 `_USE_HW_RTOS` 가 없을 때만 남겼다. 미리 켜면 스케줄러 없이 틱 처리가 돌아 죽는다(titan-mini 와 같다).
- **swtimer(버튼 1 ms) 는 틱 훅으로 옮겼다.** 전에는 SysTick 핸들러가 `swtimerISR()` 를 불렀다. 버튼을 원본(NUCLEO-C5A3ZG)처럼 1 ms 스레드로 되돌리는 것은 다음에 한다.
- **`thread cpu` 가 처음엔 전부 0 % 였다.** DWT 사이클 카운터가 멈춰 있었다. probe-rs 는 연결을 끊을 때 `DEMCR`(`0xE000_EDFC`)에 0 을 써서 `TRCENA` 를 끈다([14](14-cm7-debug.md) 로그). 기록 → 리셋 뒤 펌웨어가 켜 둔 DWT 가 그때 멈춘다. `bspGetRunTimeCounter()` 가 꺼져 있으면 다시 켜게 했다. `qspi test` · `psram test` 의 시간 측정은 매번 켜서 영향이 없었다.
- **QSPI 지우기 동안 틱을 잃는다.** `qspi` 는 ROM 호출 동안 인터럽트를 막는다(섹터 46 ms, 블록 254 ms). 그만큼 RTOS 틱이 밀린다([26](26-qspi-nor.md) 6 · 7절).
- cmd 태스크를 첫 모듈로 바꿨다(`MODULE_DEF(cmd)`, 우선순위 LOWEST, `update` 로 수신 처리). CM7 을 빌드하지 않아도 채널은 연다. ipc 스트림이 열리지 않았으면 아무것도 오가지 않는다.

## 5. 다음

- [ ] **CM7 FreeRTOS** — 포트는 `ARM_CM4F`. CM7 은 r1p2 다(CPUID `0x411FC272`). FreeRTOS 의 `ARM_CM7/r0p1` 은 r0p1 이하용이다. 네트워크(lwIP · NETC)를 CM7 에 둘 계획이다.
  - FreeRTOS 의 idle 은 `WFI` 를 하지 않고(틱리스 끔) SysTick 이 계속 돈다. 25 10절의 "WFI 에서 SysTick 이 멈춘다" 문제는 생기지 않는다.
  - CM7 cmd 태스크는 doorbell ISR 에서 세마포어를 주고 스레드가 깨어 처리한다(지금은 계속 도는 루프).
- [ ] 버튼을 1 ms 스레드로 (원본과 같이)
- [ ] event 모듈 (`eventPub` / `eventSub`, titan-mini · N657X0)
