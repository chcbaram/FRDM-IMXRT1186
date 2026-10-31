#!/usr/bin/env python3
"""
CM33 앱에 "같이 빌드한 CM7 이미지" 의 CRC 를 심는다. (docs/44)

    python3 tools/mkcm7pair.py <cm7.img | -> <out.c>

<cm7.img> 헤더(cm7_image_t)의 crc32 를 꺼내 C 상수로 쓴다. '-' 면 0 (CM7 을 같이 빌드하지 않음).
CM33 의 ipc.c 가 부팅 때 QSPI CM7 슬롯의 crc32 와 비교한다. 다르면 경고만 하고 CM7 은 그대로 깨운다.
내용이 같으면 파일을 다시 쓰지 않는다 (다시 컴파일하지 않게).

표준 라이브러리만 쓴다.
"""
import struct
import sys
from pathlib import Path

MAGIC = 0x37434D52


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    crc = 0
    if sys.argv[1] != '-':
        head = Path(sys.argv[1]).read_bytes()[:16]
        magic, size, crc, _ = struct.unpack('<IIII', head)
        if magic != MAGIC:
            print(f'mkcm7pair: {sys.argv[1]} 은 CM7 이미지가 아니다', file=sys.stderr)
            return 1

    text = ('// tools/mkcm7pair.py 가 만든다. 고치지 말 것.\n'
            '#include <stdint.h>\n\n'
            f'const uint32_t ipc_cm7_pair_crc = 0x{crc:08X}UL;   // 0 = CM7 을 같이 빌드하지 않음\n')

    out = Path(sys.argv[2])
    if not out.exists() or out.read_text() != text:
        out.write_text(text)
    print(f'mkcm7pair: CM7 crc32 0x{crc:08X} -> {out.name}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
