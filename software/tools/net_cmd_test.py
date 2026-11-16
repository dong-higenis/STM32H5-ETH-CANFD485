"""
net_cmd_test.py - 보드의 TCP 서버(포트 5000)에 접속해서 Baram 명령을 보내 보는 시험 도구

  사용:  python net_cmd_test.py 192.168.0.50

  하는 일
    1) 접속
    2) BOOT_CMD_INFO     -> mode 확인 (1 = 앱이 대답)
    3) BOOT_CMD_VERSION  -> 부트로더 / 앱 / 업데이트 버전
    4) BOOT_CMD_LED      -> 보드 LED 가 잠깐 빠르게 깜빡인다
    5) 모르는 명령        -> ERR_CMD_NO_CMD(0x0023) 이 와야 정상
    6) 망가진 봉투        -> ERR_CMD_CHECKSUM(0x0021) 이 와야 정상
    7) 왕복 시간 100번 측정

  GUI 없이 표준 Python 만으로 돈다 (설치할 것 없음).
"""
import socket
import struct
import sys
import time

PORT = 5000

PKT_TYPE_CMD  = 0x00
PKT_TYPE_RESP = 0x01

BOOT_CMD_INFO    = 0x0000
BOOT_CMD_VERSION = 0x0001
BOOT_CMD_LED     = 0x0010


def make_packet(cmd, data=b"", pkt_type=PKT_TYPE_CMD, err=0):
  """02 FD | type | cmd(2) | err(2) | len(2) | data | checksum"""
  b = bytearray([0x02, 0xFD, pkt_type, cmd & 0xFF, cmd >> 8, err & 0xFF, err >> 8, len(data) & 0xFF, len(data) >> 8])
  b += data
  b.append((-sum(b)) & 0xFF)
  return bytes(b)


class Board:
  def __init__(self, ip):
    self.sock = socket.create_connection((ip, PORT), timeout=2)
    self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    self.rx = bytearray()

  def recv_packet(self, timeout=1.0):
    end = time.time() + timeout
    while True:
      # 버퍼에 완성된 봉투가 있으면 꺼낸다
      while len(self.rx) >= 2 and not (self.rx[0] == 0x02 and self.rx[1] == 0xFD):
        del self.rx[0]
      if len(self.rx) >= 10:
        length = self.rx[7] | (self.rx[8] << 8)
        total = 9 + length + 1
        if len(self.rx) >= total:
          p = bytes(self.rx[:total])
          del self.rx[:total]
          ok = (sum(p) & 0xFF) == 0
          return {"type": p[2], "cmd": p[3] | (p[4] << 8), "err": p[5] | (p[6] << 8),
                  "data": p[9:9 + length], "checksum_ok": ok}
      left = end - time.time()
      if left <= 0:
        return None
      self.sock.settimeout(left)
      try:
        chunk = self.sock.recv(4096)
      except socket.timeout:
        return None
      if not chunk:
        raise ConnectionError("board closed the connection")
      self.rx += chunk

  def request(self, cmd, data=b""):
    self.sock.sendall(make_packet(cmd, data))
    return self.recv_packet()


def show(title, ok, detail=""):
  print("[%s] %-28s %s" % ("OK" if ok else "NG", title, detail))
  return ok


def main():
  if len(sys.argv) < 2:
    print("usage: python net_cmd_test.py <board ip>")
    return 1

  ip = sys.argv[1]
  all_ok = True

  try:
    b = Board(ip)
  except OSError as e:
    show("connect %s:%d" % (ip, PORT), False, str(e))
    return 1
  show("connect %s:%d" % (ip, PORT), True)

  r = b.request(BOOT_CMD_INFO)
  ok = r is not None and r["err"] == 0 and len(r["data"]) == 4
  all_ok &= show("BOOT_CMD_INFO", ok, "mode %d" % struct.unpack("<I", r["data"])[0] if ok else str(r))

  r = b.request(BOOT_CMD_VERSION)
  ok = r is not None and r["err"] == 0 and len(r["data"]) == 72 * 3
  all_ok &= show("BOOT_CMD_VERSION", ok)
  if ok:
    for i, name in enumerate(("boot", "firm", "update")):
      magic, ver, nm, addr = struct.unpack_from("<I32s32sI", r["data"], 72 * i)
      if magic == 0x56455220:
        print("       %-6s : %-18s %s" % (name, nm.split(b"\0")[0].decode(), ver.split(b"\0")[0].decode()))
      else:
        print("       %-6s : (none)" % name)

  r = b.request(BOOT_CMD_LED)
  all_ok &= show("BOOT_CMD_LED", r is not None and r["err"] == 0, "(LED blinks fast on the board)")

  r = b.request(0x0123)
  all_ok &= show("unknown cmd -> 0x0023", r is not None and r["err"] == 0x0023, "err 0x%04X" % r["err"] if r else "no answer")

  bad = bytearray(make_packet(BOOT_CMD_INFO))
  bad[-1] ^= 0xFF
  b.sock.sendall(bad)
  r = b.recv_packet()
  all_ok &= show("bad checksum -> 0x0021", r is not None and r["err"] == 0x0021, "err 0x%04X" % r["err"] if r else "no answer")

  times = []
  for _ in range(100):
    t0 = time.perf_counter()
    r = b.request(BOOT_CMD_INFO)
    if r is None:
      break
    times.append((time.perf_counter() - t0) * 1000)
  ok = len(times) == 100
  all_ok &= show("round trip x100", ok,
                 "avg %.1f ms, max %.1f ms" % (sum(times) / len(times), max(times)) if times else "failed")

  b.sock.close()
  print()
  print("ALL OK" if all_ok else "SOME TESTS FAILED")
  return 0 if all_ok else 1


if __name__ == "__main__":
  sys.exit(main())
