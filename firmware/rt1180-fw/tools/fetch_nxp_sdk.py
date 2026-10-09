#!/usr/bin/env python3
"""
NXP MCUXpresso SDK / CMSIS 에서 이 프로젝트가 쓰는 파일만 골라 src/lib 로 가져온다.

SDK 를 통째로 설치하거나 west 로 받지 않는다. 필요한 파일만 **커밋 SHA 를 고정해서**
GitHub raw 로 내려받는다. 받은 결과는 저장소에 커밋해 두므로 평소 빌드에는 이
스크립트도, 네트워크도 필요 없다. SDK 버전을 올리거나 파일을 추가할 때만 돌린다.

    python3 tools/fetch_nxp_sdk.py            # 목록대로 다시 받는다 (덮어쓴다)
    python3 tools/fetch_nxp_sdk.py --check    # 받은 파일이 목록과 같은지만 확인

표준 라이브러리만 쓴다 (Windows / Linux / macOS 공통).
결과 목록은 src/lib/nxp/SOURCES.md 에 남는다.
"""
import argparse
import hashlib
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent          # firmware/rt1180-fw
LIB = ROOT / 'src' / 'lib'

#-- 커밋을 고정한다. 브랜치 이름(main)을 쓰면 같은 명령이 날마다 다른 결과를 낸다.
#
REPOS = {
    'devices':  ('nxp-mcuxpresso/mcux-devices-rt',  'f7b174ef993614aaa3a23b3ca77a996c2211994c'),
    'core':     ('nxp-mcuxpresso/mcuxsdk-core',     'c6f4223f45fdab59509f25ee3ea63a71bd29d8aa'),
    'examples': ('nxp-mcuxpresso/mcuxsdk-examples', '3151bc057b8924db54c7395d39b213c05ab94dc3'),
    'cmsis':    ('ARM-software/CMSIS_6',            'v6.4.0'),
}

DEV = 'RT1180/MIMXRT1186'

#-- (저장소, 원본 경로, 저장 경로 — LIB 기준)
#
FILES = [
    #-- CMSIS Core (M-profile 만)
    *[('cmsis', f'CMSIS/Core/Include/{f}', f'cmsis/Core/Include/{f}') for f in [
        'cmsis_compiler.h', 'cmsis_gcc.h', 'cmsis_version.h', 'tz_context.h',
        'core_cm33.h', 'core_cm7.h',
        'm-profile/cmsis_gcc_m.h', 'm-profile/armv8m_mpu.h', 'm-profile/armv7m_mpu.h',
        'm-profile/armv7m_cachel1.h', 'm-profile/armv8m_pmu.h', 'm-profile/armv81m_pac.h',
    ]],
    ('cmsis', 'LICENSE', 'cmsis/LICENSE'),

    #-- 디바이스: 레지스터 헤더, SystemInit, startup
    *[('devices', f'{DEV}/{f}', f'nxp/devices/MIMXRT1186/{f}') for f in [
        'fsl_device_registers.h',
        'MIMXRT1186_cm33.h', 'MIMXRT1186_cm33_COMMON.h', 'MIMXRT1186_cm33_features.h',
        'MIMXRT1186_cm7.h',  'MIMXRT1186_cm7_COMMON.h',  'MIMXRT1186_cm7_features.h',
        'system_MIMXRT1186_cm33.c', 'system_MIMXRT1186_cm33.h',
        'system_MIMXRT1186_cm7.c',  'system_MIMXRT1186_cm7.h',
        'gcc/startup_MIMXRT1186_cm33.S', 'gcc/startup_MIMXRT1186_cm7.S',
        # 링커 스크립트는 참고용이다. 빌드는 bsp/ldscript 의 우리 스크립트를 쓴다.
        'gcc/MIMXRT1186xxxxx_cm33_flexspi_nor.ld', 'gcc/MIMXRT1186xxxxx_cm7_ram.ld',
    ]],

    #-- SoC 드라이버 (MIMXRT1186 은 RT1189 의 것을 공유한다: drivers/CMakeLists.txt)
    *[('devices', f'RT1180/MIMXRT1189/drivers/{f}', f'nxp/drivers/{f}') for f in [
        'fsl_iomuxc.h', 'fsl_clock.c', 'fsl_clock.h', 'fsl_pmu.c', 'fsl_pmu.h',
    ]],

    #-- 공통 드라이버
    *[('core', f'drivers/{f}', f'nxp/drivers/{Path(f).name}') for f in [
        'common/fsl_common.c', 'common/fsl_common.h',
        'common/fsl_common_arm.c', 'common/fsl_common_arm.h',
        'rgpio/fsl_rgpio.c', 'rgpio/fsl_rgpio.h',
    ]],

    #-- 보드: FlexSPI NOR 설정 블록의 타입 정의. 값은 bsp/boot/boot_hdr.c 에 직접 쓴다.
    ('examples', '_boards/frdmimxrt1186/xip/frdmimxrt1186_flexspi_nor_config.h',
                 'nxp/boards/frdmimxrt1186/frdmimxrt1186_flexspi_nor_config.h'),
    ('examples', '_boards/frdmimxrt1186/xip/frdmimxrt1186_flexspi_nor_config.c',
                 'nxp/boards/frdmimxrt1186/frdmimxrt1186_flexspi_nor_config.c.ref'),
]

#-- 주변장치 레지스터 헤더는 디바이스 헤더가 전부 include 하므로 통째로 받는다.
PERIPH_DIR = 'RT1180/periph'


def raw_url(repo_key, path):
    repo, ref = REPOS[repo_key]
    return f'https://raw.githubusercontent.com/{repo}/{ref}/{path}'


def api_tree(repo_key):
    import json
    repo, ref = REPOS[repo_key]
    url = f'https://api.github.com/repos/{repo}/git/trees/{ref}?recursive=1'
    with urllib.request.urlopen(url) as r:
        return json.load(r)['tree']


def fetch(url):
    with urllib.request.urlopen(url) as r:
        return r.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true', help='내려받기만 하고 비교한다')
    args = ap.parse_args()

    files = list(FILES)
    for item in api_tree('devices'):
        p = item['path']
        if item['type'] == 'blob' and p.startswith(PERIPH_DIR + '/') and p.endswith('.h'):
            files.append(('devices', p, f'nxp/devices/periph/{Path(p).name}'))

    lines = []
    diff = 0
    for repo_key, src, dst in files:
        data = fetch(raw_url(repo_key, src))
        out = LIB / dst
        sha = hashlib.sha256(data).hexdigest()[:12]
        if args.check:
            if not out.exists() or out.read_bytes() != data:
                print(f'다름  {dst}')
                diff += 1
        else:
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(data)
        lines.append(f'| `{dst}` | {REPOS[repo_key][0]} | `{src}` | `{sha}` |')
        print(f'{"확인" if args.check else "받음"}  {dst}')

    if args.check:
        print(f'\n{len(files)}개 중 {diff}개 다름')
        return 1 if diff else 0

    head = [
        '# src/lib 출처',
        '',
        '`tools/fetch_nxp_sdk.py` 가 만든 목록이다. 손으로 고치지 않는다.',
        '',
        '| 저장소 | ref |',
        '|---|---|',
        *[f'| {r} | `{ref}` |' for r, ref in REPOS.values()],
        '',
        '| 저장 경로 (src/lib 기준) | 저장소 | 원본 경로 | sha256 앞 12자 |',
        '|---|---|---|---|',
    ]
    (LIB / 'nxp' / 'SOURCES.md').write_text('\n'.join(head + sorted(lines)) + '\n')
    print(f'\n{len(files)}개 파일, 목록: src/lib/nxp/SOURCES.md')
    return 0


if __name__ == '__main__':
    sys.exit(main())
