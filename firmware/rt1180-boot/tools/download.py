#!/usr/bin/env python3
"""UART(MCU-Link VCOM) 로 앱 / CM7 / 데이터를 내려받는다. (macOS / Linux / Windows, docs/43)

  python3 tools/download.py [bin] [--target fw|cm7|data] [--offset N] [--port P] [--baud B]
  python3 tools/download.py --list          # 시리얼 포트 목록 (* = auto 가 고르는 것)

  대상
    fw   : 앱 (기본 bin: ../rt1180-fw/build/cm33/rt1180-fw-cm33.bin). TAG 를 붙이지 않은 bin 이다.
           다 쓴 뒤 부트로더가 TAG 를 기록하고 (커밋) 앱으로 점프한다 (--no-reset 으로 끈다)
    cm7  : CM7 슬롯 (기본 bin: ../rt1180-fw/build/cm7/rt1180-fw-cm7.img, RMC7 헤더가 붙은 것).
           끝나면 앱으로 점프한다. 앱이 부팅하면서 CM7 을 새 이미지로 깨운다
    data : 데이터 영역 + --offset (4 KB 단위)
  부트로더 자신(boot)은 받지 않는다. 부트 슬롯이 하나라 쓰다 끊기면 부팅이 안 된다. probe-rs 로 쓴다.

  앱이 돌고 있으면 FW_UPDATE 로 부트로더로 넘긴 뒤 다시 붙는다.
  cmd 패킷은 CLI 와 같은 UART 로 간다. 보드는 02 FD 로 시작하는 패킷만 골라낸다.
  115200 으로 붙어서 --baud 로 올리고 (기본 4 Mbps, 0 이면 그대로), 끝나면 115200 으로 되돌린다.
  baram-term 이 포트를 열고 있으면 baram-ctl 로 잠시 놓게 한다 (--no-baram 으로 끈다).

  NUCLEO-N657X0 tools/download.py 를 이 보드에 맞췄다. 필요 : pyserial
"""
import argparse
import glob
import os
import shutil
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cmdproto import *   # noqa: E402,F403

PRJ_DIR   = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
FW_DIR    = os.path.abspath(os.path.join(PRJ_DIR, "..", "rt1180-fw", "build"))
DEF_FW    = os.path.join(FW_DIR, "cm33", "rt1180-fw-cm33.bin")
DEF_CM7   = os.path.join(FW_DIR, "cm7", "rt1180-fw-cm7.img")
BAUD_DEF  = 115200
CHUNK     = 1016                 # cmd 데이터 최대 1024 B - offset 4 B
CM7_MAGIC = b"RMC7"


def list_ports_print():
  from serial.tools import list_ports
  auto = find_port()
  for p in sorted(list_ports.comports(), key=lambda p: p.device):
    vid = f"{p.vid:04X}:{p.pid:04X}" if p.vid is not None else "----:----"
    mark = "*" if p.device == auto else " "
    print(f"{mark} {p.device:<36} {vid}  {p.product or p.description or ''}")


def find_port():
  try:
    from serial.tools import list_ports
    # MCU-Link (NXP VID 0x1FC9, PID 0x0143). 맥에 다른 보드가 같이 붙어 있어도 이것만 고른다
    ml = [p.device for p in list_ports.comports()
          if p.vid == 0x1FC9 and (p.pid == 0x0143 or "MCU-LINK" in (p.product or "").upper())]
    if ml:
      return sorted(ml)[0]
  except ImportError:
    pass
  ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
  return ports[0] if ports else None


def find_baram_ctl():
  """baram-term 제어(ctl) 명령을 찾는다. VSCode 태스크처럼 PATH 에 플러그인 폴더가 없어도 찾게 한다."""
  for name, extra in (("baram-ctl", []), ("baram-term", ["ctl"])):
    path = shutil.which(name)
    if path:
      return [path] + extra
  #-- Claude Code 플러그인으로 깐 baram-term : ctl.py 는 표준 라이브러리만 쓴다
  home = os.path.expanduser("~")
  ctl = sorted(glob.glob(os.path.join(home, ".claude", "plugins", "cache", "*", "baram-term", "*",
                                      "baram-term", "src", "baram_term", "ctl.py")))
  if ctl:
    return [sys.executable, ctl[-1]]
  app = "/Applications/baram-term.app/Contents/MacOS/baram-term"
  if os.path.isfile(app):
    return [app, "ctl"]
  return None


class BaramTerm:
  """baram-term 이 포트를 쥐고 있으면 놓게 하고, 끝나면 다시 열게 한다.

  놓지 않은 채 같은 포트를 열면 macOS 는 열어 주지만 받은 바이트를 나눠 가져가 응답이 깨진다
  (VSCode 태스크에서 PATH 에 baram-ctl 이 없어 겪었다, docs/43)."""

  def __init__(self, port, enable):
    self.port = port
    self.ctl  = find_baram_ctl() if enable else None
    self.released = False

  def __enter__(self):
    if self.ctl:
      r = subprocess.run(self.ctl + ["--port", self.port, "release"], capture_output=True)
      self.released = (r.returncode == 0)
      if self.released:
        print("baram-term : 포트를 잠시 놓음")
    return self

  def __exit__(self, *exc):
    if self.released:
      subprocess.run(self.ctl + ["--port", self.port, "resume"], capture_output=True)
      print("baram-term : 포트 다시 엶")


def request_ok(ch, cmd, data=b"", timeout=3.0, what=""):
  r = ch.request(cmd, data, timeout=timeout)
  if r["err"]:
    sys.exit(f"{what or hex(cmd)} 실패 err={err_str(r['err'])}")
  return r


def change_baud(ch, tr, baud):
  """보드와 같이 보율을 바꾼다. 실패하면 115200 으로 돌아온다 (보드는 3 초 뒤 스스로 돌아온다)."""
  r = ch.request(BOOT_CMD_BAUD, struct.pack("<I", baud))
  if r["err"]:
    print(f"보율 {baud} 거부 err={err_str(r['err'])}, {BAUD_DEF} 로 진행")
    return BAUD_DEF
  tr.set_baud(baud)
  time.sleep(0.05)
  try:
    ch.request(BOOT_CMD_INFO, timeout=1.0)
    return baud
  except TimeoutError:
    print(f"보율 {baud} 에서 응답 없음. {BAUD_DEF} 로 돌아간다")
    tr.set_baud(BAUD_DEF)
    time.sleep(3.5)
    ch.request(BOOT_CMD_INFO, timeout=2.0)
    return BAUD_DEF


def download(ch, target, image, offset, args):

  if target == TARGET_FW:
    v = parse_version(ch.request(BOOT_CMD_VERSION)["data"])
    if v["img_type"]:
      print(f"현재 FW   : {v['name']} {v['version']}  [{v['img_str']}] {v['fw_size']} B  crc 0x{v['fw_crc']:04X}")
    else:
      print("현재 FW   : 없음")

  begin = struct.pack("<IB", len(image), target)
  if target == TARGET_DATA:
    begin += struct.pack("<I", offset)
  request_ok(ch, BOOT_CMD_FW_BEGIN, begin, what="BEGIN")

  t0 = time.time()
  request_ok(ch, BOOT_CMD_FW_ERASE, timeout=120.0, what="ERASE")
  print(f"  지우기 {time.time()-t0:6.2f}s")

  t0 = time.time()
  for off in range(0, len(image), CHUNK):
    r = ch.request(BOOT_CMD_FW_WRITE, struct.pack("<I", off) + image[off:off+CHUNK], timeout=3.0)
    if r["err"]:
      sys.exit(f"\nWRITE off=0x{off:X} 실패 err={err_str(r['err'])}")
    if (off // CHUNK) % 8 == 0:
      print(f"\r  쓰기   {off*100//len(image):3d}%", end="", flush=True)
  dt = time.time() - t0
  print(f"\r  쓰기   {dt:6.2f}s  ({len(image)/dt/1024:.1f} KB/s)")

  t0 = time.time()
  r = request_ok(ch, BOOT_CMD_FW_END, timeout=60.0, what="END")
  size, crc = struct.unpack("<II", r["data"][:8])
  host_crc = crc16(image[:size])
  ok = (crc == host_crc)
  print(f"  확인   {time.time()-t0:6.2f}s  {size} B  crc 0x{crc:04X} (호스트 0x{host_crc:04X}) {'OK' if ok else 'FAIL'}")
  if not ok:
    sys.exit("CRC 가 다르다")

  if target == TARGET_FW:
    r = ch.request(BOOT_CMD_FW_VERIFY, timeout=30.0)
    img = r["data"][0] if r["data"] else 0
    print(f"  판정   {IMG_TYPE[img] if img < len(IMG_TYPE) else '?'}")


def main():
  ap = argparse.ArgumentParser(description="UART 로 앱 / CM7 / 데이터 내려받기")
  ap.add_argument("binary", nargs="?", help="보낼 bin (fw: ../rt1180-fw/build/cm33/rt1180-fw-cm33.bin, cm7: ../rt1180-fw/build/cm7/rt1180-fw-cm7.img)")
  ap.add_argument("--target", choices=["fw", "cm7", "data"], default="fw")
  ap.add_argument("--offset", type=lambda s: int(s, 0), default=0, help="data 영역 안 오프셋 (4 KB 단위)")
  ap.add_argument("--port", help="시리얼 포트. 없거나 auto 면 MCU-Link VCOM 자동")
  ap.add_argument("--baud", type=int, default=4000000, help="전송 보율 (기본 4 Mbps, 0 이면 115200 그대로)")
  ap.add_argument("--no-reset", action="store_true", help="끝난 뒤 앱으로 점프하지 않는다 (부트로더에 머문다)")
  ap.add_argument("--no-baram", action="store_true", help="baram-term 포트 놓기/다시 열기를 하지 않는다")
  ap.add_argument("--list", action="store_true", help="시리얼 포트 목록만 보여 준다")
  args = ap.parse_args()

  if args.list:
    list_ports_print()
    return

  # 줄 단위로 내보내 진행 표시가 바로 보이게 한다
  sys.stdout.reconfigure(line_buffering=True)

  target = {"fw": TARGET_FW, "cm7": TARGET_CM7, "data": TARGET_DATA}[args.target]
  path = args.binary or {TARGET_FW: DEF_FW, TARGET_CM7: DEF_CM7}.get(target)
  if not path or not os.path.isfile(path):
    sys.exit(f"bin 이 없다: {path}")
  image = open(path, "rb").read()
  #-- 보드에 붙기 전에 거른다. 보율을 바꾼 뒤에 끝나면 보드가 3 초 동안 높은 보율에 남는다
  if target == TARGET_CM7 and image[:4] != CM7_MAGIC:
    sys.exit("RMC7 헤더가 없다. rt1180-fw 의 build/cm7/rt1180-fw-cm7.img 를 줄 것 (-DBUILD_CM7=ON)")

  port = args.port if args.port and args.port.lower() != "auto" else find_port()
  if not port:
    sys.exit("시리얼 포트를 찾지 못했다. --port 로 줄 것")

  import serial
  with BaramTerm(port, not args.no_baram):
    ser = serial.Serial(port, BAUD_DEF, timeout=0.2)
    tr  = SerialTransport(ser)
    ch  = CmdChannel(tr)
    try:
      info = parse_info(ch.request(BOOT_CMD_INFO, timeout=2.0)["data"])
      print(f"연결     : {port}  {info['name']} {info['version']}  [{info['mode_str']}]")
      if info["mode"] != DEV_MODE_BOOT:
        # 앱이 돌고 있다. 부트로더에 머물러 달라고 하고(FW_UPDATE → resetToBoot) 다시 붙는다
        print("         앱이 실행 중 → 부트로더로 넘어가 다시 붙는다")
        try:
          ch.request(BOOT_CMD_FW_UPDATE, timeout=1.0)
        except TimeoutError:
          pass
        time.sleep(1.5)
        info = parse_info(ch.request(BOOT_CMD_INFO, timeout=3.0)["data"])
        print(f"연결     : {port}  {info['name']} {info['version']}  [{info['mode_str']}]")
        if info["mode"] != DEV_MODE_BOOT:
          sys.exit("부트로더로 넘어가지 않았다")
      if info["cmd_ver"] < 2:
        sys.exit("CM7 슬롯이 있는 INFO(cmd_ver 2)가 아니다 (다른 보드의 부트로더?)")

      print(f"보낼 것  : {os.path.basename(path)}  {len(image)/1024:.1f} KB -> {TARGET_STR[target]}"
            + (f" +0x{args.offset:X}" if target == TARGET_DATA else ""))

      baud = BAUD_DEF
      if args.baud and args.baud != BAUD_DEF:
        baud = change_baud(ch, tr, args.baud)
      print(f"보율     : {baud}")

      t0 = time.time()
      download(ch, target, image, args.offset, args)
      print(f"합계     : {time.time()-t0:.2f}s")

      if baud != BAUD_DEF:
        try:
          ch.request(BOOT_CMD_BAUD, struct.pack("<I", BAUD_DEF), timeout=1.0)
        except TimeoutError:
          pass
        tr.set_baud(BAUD_DEF)

      if target in (TARGET_FW, TARGET_CM7) and not args.no_reset:
        r = ch.request(BOOT_CMD_FW_JUMP, timeout=2.0)
        print("실행     : 앱으로 점프" if r["err"] == 0 else f"실행     : 실패 err={err_str(r['err'])}")
    finally:
      ser.close()


if __name__ == "__main__":
  main()
