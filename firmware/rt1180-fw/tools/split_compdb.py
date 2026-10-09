#!/usr/bin/env python3
"""통합 compile_commands.json 을 코어별로 갈라 놓는다.

CMake 프로젝트가 하나라서(flash 가 두 ELF 를 함께 올리고, 나중에 mkimage.py 가
두 bin 을 한 이미지로 묶어야 한다) 빌드 트리도 하나고 compile_commands.json 도 한 벌이다.

문제는 두 코어가 **같은 SDK 소스를 서로 다른 -mcpu 로** 컴파일한다는 점이다.
fsl_common.c 같은 공유 파일이 항목 두 개로 들어가고, IntelliSense 는 먼저 찾은 것을
쓰므로 CM7 워크스페이스에서 M33 기준으로 해석되는 일이 생긴다.

그래서 빌드 트리는 그대로 두고 compile_commands.json 만 코어별로 만든다.

    build/compile_commands.json        ← CMake 가 만든 통합본 (그대로 둔다)
    build/cm33/compile_commands.json   ← 이 스크립트가 만든다
    build/cm7/compile_commands.json

각 워크스페이스는 자기 코어 것을 가리킨다.
"""
import json
import re
import sys
from pathlib import Path

# output 은 ".../build/src/cpu/cm33/CMakeFiles/rt1180-fw-cm33.elf.dir/....obj" 꼴이다.
CORE = re.compile(r'/src/cpu/([^/]+)/CMakeFiles/')


def main() -> int:
    if len(sys.argv) != 2:
        print('사용법: split_compdb.py <빌드 디렉터리>', file=sys.stderr)
        return 2

    build = Path(sys.argv[1])
    merged = build / 'compile_commands.json'
    if not merged.exists():
        # CMAKE_EXPORT_COMPILE_COMMANDS 를 지원하지 않는 제너레이터일 수 있다.
        # 빌드를 멈출 이유는 아니다.
        print(f'compile_commands.json 이 없다: {merged}')
        return 0

    entries = json.loads(merged.read_text(encoding='utf-8'))

    per_core: dict = {}
    for e in entries:
        m = CORE.search(e.get('output', ''))
        if m:
            per_core.setdefault(m.group(1), []).append(e)

    if not per_core:
        print('코어를 구분할 수 없다 — 통합본만 둔다')
        return 0

    for core, items in sorted(per_core.items()):
        out_dir = build / core
        out_dir.mkdir(parents=True, exist_ok=True)
        out = out_dir / 'compile_commands.json'
        new = json.dumps(items, indent=2) + '\n'

        # 내용이 같으면 건드리지 않는다. mtime 이 바뀌면 cpptools 가 매번 다시 판다.
        if out.exists() and out.read_text(encoding='utf-8') == new:
            continue
        out.write_text(new, encoding='utf-8')
        print(f'{core}: {len(items)} 개 → {out.relative_to(build)}')

    return 0


if __name__ == '__main__':
    sys.exit(main())
