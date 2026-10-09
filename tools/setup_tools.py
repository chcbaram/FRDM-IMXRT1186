#!/usr/bin/env python3
"""
새 PC 에서 이 저장소를 받았을 때 개발 환경을 점검하고 외부 자산을 준비한다.

    python3 tools/setup_tools.py            # 점검 + SVD 준비
    python3 tools/setup_tools.py --check    # 점검만 (아무것도 받지 않는다)

Windows / Linux / macOS 공통. 표준 라이브러리만 쓴다.

하는 일
  1. 도구 점검   arm-none-eabi-gcc, cmake, ninja, probe-rs, python 의 존재와 버전.
                 없거나 낮으면 OS 별 설치 방법을 알려 준다. 설치는 직접 하지 않는다.
  2. SVD 준비    디버거 레지스터 뷰용. NXP 공개 CMSIS 팩(MIMXRT1186_DFP)에서 꺼내
                 firmware/rt1180-fw/tools/svd/ 에 둔다. 한 파일이 70 MB 넘어서
                 저장소에는 넣지 않는다.

SDK 소스는 이미 저장소에 vendoring 되어 있어 받을 필요가 없다
(firmware/rt1180-fw/src/lib, 출처는 src/lib/nxp/SOURCES.md).

절차 전체는 firmware/docs/13-os-setup.md.
"""
import argparse
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FW = ROOT / 'firmware' / 'rt1180-fw'
SVD_DIR = FW / 'tools' / 'svd'

DFP_VERSION = '26.09.00'
DFP_URL = f'https://mcuxpresso.nxp.com/cmsis_pack/repo/NXP.MIMXRT1186_DFP.{DFP_VERSION}.pack'
SVD_FILES = {
    'devices/MIMXRT1186/MIMXRT1186_cm33.xml': 'MIMXRT1186_cm33.xml',
    'devices/MIMXRT1186/MIMXRT1186_cm7.xml':  'MIMXRT1186_cm7.xml',
}

#-- (이름, 명령, 버전 정규식, 최소 버전, 검증한 버전)
#   최소 버전 근거는 docs/10-dev-environment.md
#
TOOLS = [
    ('arm-none-eabi-gcc', ['arm-none-eabi-gcc', '--version'], r'(\d+\.\d+\.\d+)', '13.2.0', '15.3.1'),
    ('cmake',             ['cmake', '--version'],             r'(\d+\.\d+\.\d+)', '3.20.0', '4.4.3'),
    ('ninja',             ['ninja', '--version'],             r'(\d+\.\d+\.\d+)', '1.10.0', '1.13.2'),
    ('probe-rs',          ['probe-rs', '--version'],          r'(\d+\.\d+\.\d+)', '0.30.0', '0.32.0'),
]

INSTALL_HINT = {
    'Darwin': {
        'arm-none-eabi-gcc': 'brew install --cask gcc-arm-embedded',
        'cmake':             'brew install cmake',
        'ninja':             'brew install ninja',
        'probe-rs':          'brew install probe-rs-tools',
    },
    'Linux': {
        'arm-none-eabi-gcc': 'Arm GNU Toolchain 을 developer.arm.com 에서 받아 PATH 에 추가 (배포판 패키지는 버전이 낮을 수 있다)',
        'cmake':             'sudo apt install cmake   (또는 pip install cmake)',
        'ninja':             'sudo apt install ninja-build',
        'probe-rs':          "curl -LsSf https://github.com/probe-rs/probe-rs/releases/latest/download/probe-rs-tools-installer.sh | sh",
    },
    'Windows': {
        'arm-none-eabi-gcc': 'Arm GNU Toolchain 설치본(.exe)을 developer.arm.com 에서 받아 설치, 또는 ARM_TOOLCHAIN_DIR 지정',
        'cmake':             'winget install Kitware.CMake',
        'ninja':             'winget install Ninja-build.Ninja',
        'probe-rs':          'powershell -c "irm https://github.com/probe-rs/probe-rs/releases/latest/download/probe-rs-tools-installer.ps1 | iex"',
    },
}


def ver_tuple(v: str):
    return tuple(int(x) for x in v.split('.'))


def check_tools() -> bool:
    osname = platform.system()
    ok = True

    print(f'== 도구 점검 ({osname} {platform.machine()})')
    print(f'   python {platform.python_version()}  {"OK" if sys.version_info >= (3, 8) else "3.8 이상 필요"}')

    for name, cmd, pat, minimum, tested in TOOLS:
        exe = shutil.which(cmd[0])
        if not exe:
            print(f'   {name:18s} 없음      → {INSTALL_HINT.get(osname, {}).get(name, "")}')
            ok = False
            continue

        out = subprocess.run(cmd, capture_output=True, text=True).stdout
        m = re.search(pat, out)
        ver = m.group(1) if m else '?'

        if m and ver_tuple(ver) < ver_tuple(minimum):
            print(f'   {name:18s} {ver:9s} 최소 {minimum} 필요 → {INSTALL_HINT.get(osname, {}).get(name, "")}')
            ok = False
        else:
            note = '' if ver == tested else f'(검증 버전 {tested})'
            print(f'   {name:18s} {ver:9s} OK {note}')

    return ok


def check_probe():
    if not shutil.which('probe-rs'):
        return
    out = subprocess.run(['probe-rs', 'list'], capture_output=True, text=True)
    lines = [l for l in (out.stdout + out.stderr).splitlines() if 'MCU-LINK' in l or 'CMSIS-DAP' in l]
    print('\n== 디버그 프로브')
    if lines:
        for l in lines:
            print(f'   {l.strip()}')
    else:
        print('   찾지 못했다. J23(MCU-Link USB) 연결을 확인한다. 리눅스는 udev 규칙이 필요하다 (13-os-setup.md).')


def prepare_svd(check_only: bool) -> bool:
    print('\n== SVD')
    missing = [dst for dst in SVD_FILES.values() if not (SVD_DIR / dst).exists()]
    if not missing:
        print(f'   있음  {SVD_DIR.relative_to(ROOT)}')
        return True
    if check_only:
        print(f'   없음  {", ".join(missing)}  → python3 tools/setup_tools.py')
        return False

    print(f'   받는 중  {DFP_URL}')
    SVD_DIR.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        pack = Path(tmp) / 'dfp.pack'
        urllib.request.urlretrieve(DFP_URL, pack)
        with zipfile.ZipFile(pack) as z:
            for src, dst in SVD_FILES.items():
                (SVD_DIR / dst).write_bytes(z.read(src))
                print(f'   꺼냄  {dst}')
    return True


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true', help='점검만 한다')
    args = ap.parse_args()

    ok = check_tools()
    check_probe()
    ok = prepare_svd(args.check) and ok

    print('\n' + ('준비 완료. 빌드: cd firmware/rt1180-fw && cmake -S . -B build -G Ninja && cmake --build build'
                  if ok else '위 항목을 해결한 뒤 다시 실행한다.'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
