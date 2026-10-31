#!/usr/bin/env python3
"""
probe-rs GDB 서버를 뒤에서 띄우고 내린다. VSCode Cortex-Debug 구성이 쓴다. (docs/42 5절)

    python3 tools/gdbserver.py start --pidfile build/probe-rs-gdb.pid [--chip MIMXRT1180] [--port 1337]
    python3 tools/gdbserver.py stop  --pidfile build/probe-rs-gdb.pid

start : 남아 있는 서버(pid 파일)를 먼저 내리고, 새 서버를 떼어 낸 프로세스로 띄운 뒤
        "Firing up GDB stub" 이 로그에 나오면 끝난다. 끝나는 태스크라서 VSCode 의
        백그라운드 태스크 준비 판정(problemMatcher)에 기대지 않는다.
stop  : pid 파일의 서버를 내린다. 서버가 프로브를 잡고 있으면 다음 기록(flash)이 실패한다.

로그는 pid 파일 옆 probe-rs-gdb.log. 표준 라이브러리만 쓴다.
"""
import argparse
import os
import signal
import subprocess
import sys
import time

IS_WIN = os.name == "nt"
READY  = "Firing up GDB stub"


def is_server(pid):
  """pid 가 살아 있는 probe-rs 인지. 다른 프로세스가 같은 pid 를 받았으면 건드리지 않는다."""
  try:
    if IS_WIN:
      out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"],
                           capture_output=True, text=True).stdout
    else:
      out = subprocess.run(["ps", "-p", str(pid), "-o", "command="],
                           capture_output=True, text=True).stdout
  except OSError:
    return False
  return "probe-rs" in out


def stop(pidfile):
  try:
    with open(pidfile) as f:
      pid = int(f.read().strip())
  except (OSError, ValueError):
    return
  if is_server(pid):
    os.kill(pid, signal.SIGTERM)
    for _ in range(50):
      if not is_server(pid):
        break
      time.sleep(0.1)
    print(f"gdb server stopped (pid {pid})")
  try:
    os.remove(pidfile)
  except OSError:
    pass


def start(args):
  stop(args.pidfile)

  os.makedirs(os.path.dirname(os.path.abspath(args.pidfile)), exist_ok=True)
  log_path = os.path.join(os.path.dirname(os.path.abspath(args.pidfile)), "probe-rs-gdb.log")
  cmd = ["probe-rs", "gdb", "--chip", args.chip,
         "--gdb-connection-string", f"127.0.0.1:{args.port}"]
  if args.chip_description_path:
    cmd += ["--chip-description-path", args.chip_description_path]

  kw = {}
  if IS_WIN:
    kw["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.DETACHED_PROCESS
  else:
    kw["start_new_session"] = True

  log = open(log_path, "w")
  proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, **kw)
  with open(args.pidfile, "w") as f:
    f.write(str(proc.pid))

  end = time.time() + args.timeout
  while time.time() < end:
    with open(log_path, errors="replace") as f:
      text = f.read()
    if READY in text:
      print(text.strip())
      print(f"gdb server ready (pid {proc.pid}, 127.0.0.1:{args.port})")
      return 0
    if proc.poll() is not None:
      print(text.strip())
      print(f"gdb server exited ({proc.returncode})")
      return 1
    time.sleep(0.1)

  print("gdb server did not start in time")
  stop(args.pidfile)
  return 1


def main():
  ap = argparse.ArgumentParser()
  ap.add_argument("action", choices=["start", "stop"])
  ap.add_argument("--pidfile", required=True)
  ap.add_argument("--chip", default="MIMXRT1180")
  ap.add_argument("--chip-description-path")
  ap.add_argument("--port", type=int, default=1337)
  ap.add_argument("--timeout", type=float, default=10.0)
  args = ap.parse_args()

  if args.action == "stop":
    stop(args.pidfile)
    return 0
  return start(args)


if __name__ == "__main__":
  sys.exit(main())
