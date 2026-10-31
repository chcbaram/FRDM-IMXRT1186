#!/usr/bin/env python3
"""
앱 bin 앞에 TAG 섹터(4 KB)를 붙인다. (docs/42-app-jump.md)

    python3 tools/mkfwtag.py build/cm33/rt1180-fw-cm33.bin
      → build/cm33/rt1180-fw-cm33-tag.bin   (0x0404_0000 에 쓴다)

부트로더(rt1180-boot)는 TAG 가 맞을 때만 앱으로 점프한다. UART 다운로드(43)에서는
부트로더가 TAG 를 쓰지만, probe-rs 로 쓸 때는 이 스크립트가 같은 형식으로 만든다.
NUCLEO-N657X0 tools/flash.py 의 make_tagged() 와 같다.

  firm_tag_t { magic "TAG ", fw_addr = TAG 크기, fw_size, fw_crc, tag_crc }
    fw_crc  : 벡터부터 fw_size 바이트의 CRC-16
    tag_crc : 앞 16 바이트의 CRC-16
  fw_size 는 이미지 안 firm_ver_t.firm_size(링커 값)를 우선한다.

표준 라이브러리만 쓴다.
"""
import os
import struct
import sys

TAG_MAGIC    = 0x54414720     # "TAG "
VER_MAGIC    = 0x56455220     # "VER "
TAG_SIZE     = 0x1000
VER_OFFSET   = 0x400          # 벡터 테이블 바로 뒤 (링커 .version)
VER_SIZE_OFF = VER_OFFSET + 4 + 32 + 32 + 4     # firm_ver_t.firm_size


#-- 펌웨어 util_core.c 의 utilCalcCRC 와 같다 (CRC-16, poly 0x8005, MSB first, 초기값 0)
def _crc_table():
  table = []
  for i in range(256):
    crc = i << 8
    for _ in range(8):
      crc = ((crc << 1) ^ 0x8005) if (crc & 0x8000) else (crc << 1)
    table.append(crc & 0xFFFF)
  return table

_CRC_TABLE = _crc_table()


def crc16(data, crc=0):
  for b in data:
    crc = ((crc << 8) ^ _CRC_TABLE[((crc >> 8) ^ b) & 0xFF]) & 0xFFFF
  return crc


def make_tagged(bin_path):
  with open(bin_path, "rb") as f:
    image = f.read()

  size = len(image)
  if len(image) >= VER_SIZE_OFF + 4:
    magic, = struct.unpack_from("<I", image, VER_OFFSET)
    firm_size, = struct.unpack_from("<I", image, VER_SIZE_OFF)
    if magic == VER_MAGIC and 0 < firm_size <= len(image):
      size = firm_size

  crc  = crc16(image[:size])
  head = struct.pack("<4I", TAG_MAGIC, TAG_SIZE, size, crc)
  tag  = head + struct.pack("<I", crc16(head))
  out  = os.path.splitext(bin_path)[0] + "-tag.bin"
  with open(out, "wb") as f:
    f.write(tag + b"\xFF" * (TAG_SIZE - len(tag)) + image)
  print(f"TAG  : {size} B  crc 0x{crc:04X}  -> {os.path.basename(out)}")
  return out


if __name__ == "__main__":
  if len(sys.argv) != 2:
    print("usage: mkfwtag.py <app.bin>")
    sys.exit(1)
  make_tagged(sys.argv[1])
