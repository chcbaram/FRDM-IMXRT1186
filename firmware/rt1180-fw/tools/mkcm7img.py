#!/usr/bin/env python3
"""
CM7 bin 앞에 헤더를 붙여 QSPI CM7 슬롯(0x0480_0000)에 쓸 이미지를 만든다.

    python3 tools/mkcm7img.py <in.bin> <out.img>

헤더는 src/cpu/shared/shared.h 의 cm7_image_t 와 같아야 한다 (16 B, 리틀 엔디언).

    magic   0x37434D52 ("RMC7")
    size    이미지 바이트 수 (헤더 제외)
    crc32   이미지의 CRC-32 (zlib.crc32 와 같은 IEEE 802.3 반사형)
    reserved 0

CM33 은 기동 전에 이 셋을 확인하고, 맞을 때만 CM7 ITCM 으로 복사한다.
표준 라이브러리만 쓴다.
"""
import struct
import sys
import zlib
from pathlib import Path

MAGIC = 0x37434D52
MAX_SIZE = 256 * 1024


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    data = Path(sys.argv[1]).read_bytes()
    if len(data) == 0 or len(data) > MAX_SIZE:
        print(f'mkcm7img: 크기 {len(data)} B 가 범위(1 ~ {MAX_SIZE}) 밖이다', file=sys.stderr)
        return 1

    crc = zlib.crc32(data) & 0xFFFFFFFF
    header = struct.pack('<IIII', MAGIC, len(data), crc, 0)
    Path(sys.argv[2]).write_bytes(header + data)

    print(f'mkcm7img: {len(data)} B, crc32 0x{crc:08X} -> {sys.argv[2]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
