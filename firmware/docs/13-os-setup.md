# 13. 새 PC 에서 환경 구축

> macOS / Windows / Linux 에서 같은 환경을 만들어 빌드, 기록, 디버그까지 가는 절차.
> 관련: [10-dev-environment.md](10-dev-environment.md) · [12-project-skeleton.md](12-project-skeleton.md)
>
> **실제로 검증한 것은 macOS(Apple Silicon)뿐이다.** Windows 와 Linux 절차는 각 도구의 공식 설치 방법을 따라 적었다. 아직 돌려 보지 않았으며, 돌려 본 뒤 이 문서를 고친다.

---

## 0. 무엇이 필요한가

| 도구 | 검증 버전 | 최소 |
|---|---|---|
| Arm GNU Toolchain | 15.3.Rel1 | 13.2 |
| CMake | 4.4.3 | 3.20 |
| Ninja | 1.13.2 | 1.10 |
| probe-rs | 0.32.0 | 0.30 |
| Python | 3.9 | 3.8 |
| VSCode 확장 | `probe-rs.probe-rs-debugger`, `ms-vscode.cpptools` | |

어느 OS 든 마지막은 같다.

```bash
python3 tools/setup_tools.py      # 모두 OK 가 나오면 끝
```

## 1. macOS (검증함)

### 1-1. 도구 설치

```bash
# Homebrew 가 없다면 https://brew.sh
brew install --cask gcc-arm-embedded     # 또는 developer.arm.com 설치본
brew install cmake ninja probe-rs-tools
```

### 1-2. 저장소와 자산

```bash
git clone https://github.com/chcbaram/FRDM-IMXRT1186.git
cd FRDM-IMXRT1186
python3 tools/setup_tools.py             # SVD 를 받는다 (약 7 MB 팩 → 143 MB 추출)
```

### 1-3. 시리얼 콘솔 (로드맵 21 이후)

MCU-Link VCOM 이 `/dev/cu.usbmodem*` 로 잡힌다.

## 2. Windows (미검증)

### 2-1. 도구 설치

```powershell
winget install Kitware.CMake
winget install Ninja-build.Ninja
winget install Python.Python.3.12
# Arm GNU Toolchain : developer.arm.com 에서 arm-gnu-toolchain-*-mingw-w64-*-arm-none-eabi.exe 설치
#   설치 마지막의 "Add path to environment variable" 을 체크하거나, ARM_TOOLCHAIN_DIR 을 지정한다
# probe-rs
powershell -ExecutionPolicy Bypass -c "irm https://github.com/probe-rs/probe-rs/releases/latest/download/probe-rs-tools-installer.ps1 | iex"
```

### 2-2. 드라이버

MCU-Link 의 CMSIS-DAP v2 는 WinUSB 인터페이스라 보통 별도 드라이버가 필요 없다. `probe-rs list` 에 안 보이면 [6절](#6-막혔을-때) 을 본다.

### 2-3. 저장소와 자산

```powershell
git clone https://github.com/chcbaram/FRDM-IMXRT1186.git
cd FRDM-IMXRT1186
python tools\setup_tools.py
```

> `git config core.autocrlf` 가 `true` 여도 `.gitattributes` 가 LF 로 고정하므로 링커 스크립트는 안전하다.

## 3. Linux — Ubuntu / Debian (미검증)

### 3-1. 도구 설치

```bash
sudo apt install cmake ninja-build python3 git
# Arm GNU Toolchain : 배포판 패키지(gcc-arm-none-eabi)는 버전이 낮을 수 있다. 공식 tarball 을 쓴다
#   https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
#   x86_64 Linux hosted → arm-none-eabi 를 받아 /opt 에 풀고 PATH 에 bin 을 추가
curl -LsSf https://github.com/probe-rs/probe-rs/releases/latest/download/probe-rs-tools-installer.sh | sh
```

### 3-2. USB 권한

일반 사용자로 프로브에 접근하려면 udev 규칙이 필요하다(probe-rs 공식 안내).

```bash
sudo curl -o /etc/udev/rules.d/69-probe-rs.rules https://probe.rs/files/69-probe-rs.rules
sudo udevadm control --reload && sudo udevadm trigger
# plugdev 그룹에 속해 있어야 한다:  sudo usermod -aG plugdev $USER  (재로그인)
```

### 3-3. 저장소와 자산

macOS 와 같다.

## 4. 공통 — 빌드와 기록

보드 준비는 다음과 같다([10-dev-environment.md](10-dev-environment.md) 4절).

- J60 을 `100` 으로 둔다.
- USB 는 J23 에 꽂는다.

```bash
cd firmware/rt1180-fw
cmake -S . -B build -G Ninja
cmake --build build
cmake --build build --target flash        # 녹색 LED 500 ms 점멸
```

같은 환경인지 확인하는 방법은 다음과 같다.

```bash
python3 tools/fetch_nxp_sdk.py --check    # 148개 중 0개 다름
python3 tools/check_layers.py             # 위반 0건
```

## 5. 공통 — VSCode

### 5-1. 확장

```bash
code --install-extension probe-rs.probe-rs-debugger
code --install-extension ms-vscode.cpptools
```

probe-rs 확장은 이름이 세 가지라 헷갈린다. 모두 같은 확장이다.

| 구분 | 값 |
|---|---|
| Marketplace 표시 이름 | **Debugger for probe-rs** (검색은 `probe-rs` 로) |
| 확장 ID | `probe-rs.probe-rs-debugger` |
| 디버그 구성의 `"type"` | `probe-rs-debug` |

확장은 디버거를 따로 들고 오지 않고 PATH 의 `probe-rs` 를 실행한다. 확장 버전과 probe-rs 버전을 맞춘다(둘 다 0.32.0).

### 5-2. 워크스페이스

`firmware/rt1180-fw/prj/rt1180-fw-cm33.code-workspace` 를 연다(File → Open Workspace from File).

### 5-3. 빌드와 디버그

| 작업 | 방법 |
|---|---|
| 빌드 | `Cmd/Ctrl + Shift + B` (`build-build`) |
| 기록 | 태스크 `flash` |
| 디버그 | 실행 및 디버그 → `Debug CM33` (빌드 → 기록 → 리셋 후 정지) |
| 실행 중 붙기 | `Attach CM33` (기록하지 않는다) |

`Debug CM33` 은 시작할 때마다 elf 를 기록한다. `verifyBeforeFlashing` 이 켜져 있어 내용이 같으면 쓰지 않는다. 기록 없이 붙으려면 `Attach CM33` 을 쓴다.

#### main 에서 멈추려면

probe-rs 확장에는 cortex-debug 의 `runToEntryPoint` 같은 옵션이 없다. `haltAfterReset` 으로 리셋 직후 멈추면 **BootROM 안**이다. 이 칩은 리셋되면 ROM 부터 돌기 때문이다.

1. `main.c` 의 `bspInit();` 에 브레이크포인트를 건다. VSCode 가 워크스페이스별로 기억하므로 한 번만 하면 된다.
2. `Debug CM33` 을 시작한다. 소스가 없는 ROM 주소에서 멈춘다.
3. F5 를 누른다. ROM 이 FlexSPI 와 컨테이너를 처리한 뒤 `main` 에서 멈춘다.

같은 흐름을 명령줄 GDB 로 확인한 결과는 다음과 같다.

```
$ probe-rs gdb --chip MIMXRT1180 --reset-halt     # 다른 터미널에서 arm-none-eabi-gdb 로 접속
pc 0x1001676c                                     # 리셋 직후 = CM33 BootROM (Secure)
Breakpoint 1 at 0x400c742: main.c:6               # 플래시라 하드웨어 브레이크포인트
Breakpoint 1, main () at main.c:6
```

> 태스크가 워크스페이스 파일에 있는 이유는 [12-project-skeleton.md](12-project-skeleton.md) 5절에 있다. `.vscode/tasks.json` 에 두면 *"Could not find the task 'build-build'"* 가 뜬다.

## 6. 막혔을 때

### `probe-rs list` 에 프로브가 안 보인다

- J23(MCU-Link USB)에 꽂았는지 본다. J63(USB OTG)이 아니다.
- J40 이 단락되어 있으면 MCU-Link 가 ISP 모드로 뜬다. 개방한다.
- Linux 는 udev 규칙(3-2), Windows 는 장치 관리자에서 `MCU-LINK CMSIS-DAP` 인터페이스를 확인한다.

### 기록은 되는데 LED 가 안 깜빡인다

- J60 이 `100` 인가. 바꿨다면 SW2 로 POR 을 건다.
- `probe-rs read --chip MIMXRT1180 b32 0x04001000 4` 의 첫 워드가 `8700a000` 인가. 컨테이너 tag 와 길이다.
- `probe-rs read --chip MIMXRT1180 b32 0x44460044 1` 이 `0c000000` 인가. CM33 + FlexSPI NOR 부팅이라는 뜻이다.

### 링크 경고 `_write is not implemented` 가 다시 보인다

`bsp/syscalls.c` 가 빌드에서 빠진 것이다. SDK 드라이버의 `assert` 가 stdio 를 끌어오기 때문에 시스템콜을 우리가 정의해야 한다([20-led.md](20-led.md) 8절).

### 공장 데모로 되돌리고 싶다

`hardware/ref/factory/factory_64k.bin` 이 첫 기록 전에 읽어 둔 앞 64 KB 다. 이 파일은 저장소에 없다. 원래 보드에서만 만들 수 있다.

```bash
probe-rs download --chip MIMXRT1180 --binary-format bin --base-address 0x04000000 hardware/ref/factory/factory_64k.bin
```
