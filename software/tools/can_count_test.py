"""
can_count_test.py - CAN 손실 숫자(카운터)가 맞게 세어지는지 확인 (외부 CAN 장치 없이)

  사용:  python can_count_test.py 192.168.0.50

  보드의 손실 숫자는 CMD_CAN_GET_STAT(0x0116) 로 랜에서 읽는다.

  1) 루프백 200 프레임   -> 보냄 = 나감 = 받음 = 200, 손실 0
  2) 모니터 모드 2초     -> 보드는 아무것도 안 내보내고, CAN 선에 다른 장치가 말하는지만 본다
  3) 선이 조용하면: 일반 모드로 10 프레임
                         -> 받아 줄 장치가 없으면 100ms 뒤 포기(timeout)로 세어져야 한다
                            (장치가 있으면 나감(sent)으로 세어진다. 어느 쪽이든 보냄 = 나감 + 포기)
"""
import socket
import struct
import sys
import time

PORT = 5000
PKT_TYPE_CMD, PKT_TYPE_RESP, PKT_TYPE_CAN = 0x00, 0x01, 0x05
CMD_VERSION = 0x0001
CMD_CAN_OPEN, CMD_CAN_CLOSE, CMD_CAN_DATA, CMD_CAN_GET_STAT = 0x0110, 0x0111, 0x0112, 0x0116
CAN_NORMAL, CAN_MONITOR, CAN_LOOPBACK = 0, 1, 2
CAN_CLASSIC, CAN_FD_BRS = 0, 2
CAN_STD = 0
CAN_500K, CAN_2M = 3, 5

STAT_NAMES = ["rx", "rx_drop", "rx_fifo_lost", "lan_drop",
              "queued", "sent", "timeout", "queue_full", "pending",
              "tec", "rec", "bus_off", "last_err"]

fail = 0


def check(name, ok, extra=""):
  global fail
  print("[%s] %s %s" % ("OK" if ok else "NG", name, extra), flush=True)
  if not ok:
    fail += 1


def make_packet(pkt_type, cmd, data=b""):
  b = bytearray([0x02, 0xFD, pkt_type, cmd & 0xFF, cmd >> 8, 0, 0, len(data) & 0xFF, len(data) >> 8])
  b += data
  b.append((-sum(b)) & 0xFF)
  return bytes(b)


class Board:
  def __init__(self, ip):
    self.sock = socket.create_connection((ip, PORT), timeout=2)
    self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    self.rx = bytearray()
    self.can_rx = 0

  def _next_packet(self):
    while len(self.rx) >= 2 and not (self.rx[0] == 0x02 and self.rx[1] == 0xFD):
      del self.rx[0]
    if len(self.rx) < 10:
      return None
    n = 10 + (self.rx[7] | self.rx[8] << 8)
    if len(self.rx) < n:
      return None
    p = bytes(self.rx[:n]); del self.rx[:n]
    return {"type": p[2], "cmd": p[3] | p[4] << 8, "err": p[5] | p[6] << 8, "data": p[9:-1]}

  def collect(self, timeout, want_resp=True):
    end = time.time() + timeout
    while time.time() < end:
      p = self._next_packet()
      if p:
        if p["type"] == PKT_TYPE_CAN:
          self.can_rx += 1
          continue
        if want_resp:
          return p
        continue
      self.sock.settimeout(max(0.01, end - time.time()))
      try:
        d = self.sock.recv(65536)
        if not d:
          raise ConnectionError("closed")
        self.rx += d
      except socket.timeout:
        pass
    return None

  def cmd(self, cmd, data=b"", timeout=1.0):
    self.sock.sendall(make_packet(PKT_TYPE_CMD, cmd, data))
    return self.collect(timeout)

  def stat(self, clear=False):
    p = self.cmd(CMD_CAN_GET_STAT, bytes([1 if clear else 0]))
    if p is None or p["err"] != 0 or len(p["data"]) < 52:
      return None
    return dict(zip(STAT_NAMES, struct.unpack("<13I", p["data"][:52])))

  def open(self, mode, frame, baud=CAN_500K, baud_data=CAN_2M):
    p = self.cmd(CMD_CAN_OPEN, bytes([mode, frame, baud, baud_data]))
    return p is not None and p["err"] == 0

  def send_can(self, n, frame, length, can_id=0x7F0):
    dlc = {0: 0, 8: 8, 64: 15}[length]
    for i in range(n):
      data = struct.pack("<I", i) + bytes(length - 4) if length >= 4 else bytes(length)
      body = struct.pack("<HBBBIB", 0, frame, CAN_STD, dlc, can_id, length) + data
      self.sock.sendall(make_packet(PKT_TYPE_CAN, CMD_CAN_DATA, body))


def show(s):
  print("      queued %(queued)d = sent %(sent)d + timeout %(timeout)d + pending %(pending)d,"
        " queue_full %(queue_full)d" % s)
  print("      rx %(rx)d, rx_fifo_lost %(rx_fifo_lost)d, rx_drop %(rx_drop)d, lan_drop %(lan_drop)d,"
        " TEC %(tec)d REC %(rec)d bus_off %(bus_off)d" % s)


def main():
  ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.50.107"
  b = Board(ip)

  p = b.cmd(CMD_VERSION)
  if p:
    ver = p["data"][72 + 4: 72 + 36].split(b"\0")[0].decode(errors="replace")
    print("app : %s" % ver)

  # 1) 루프백
  b.stat(clear=True)
  check("open loopback", b.open(CAN_LOOPBACK, CAN_FD_BRS))
  b.can_rx = 0
  b.send_can(200, CAN_FD_BRS, 64)
  b.collect(1.5, want_resp=False)
  s = b.stat(clear=True)
  show(s)
  check("loopback counts", s["queued"] == 200 and s["sent"] == 200 and s["rx"] == 200 and b.can_rx == 200
        and s["timeout"] == 0 and s["queue_full"] == 0 and s["rx_fifo_lost"] == 0 and s["rx_drop"] == 0
        and s["lan_drop"] == 0, "(PC got %d)" % b.can_rx)
  check("loopback identity queued = sent + timeout + pending",
        s["queued"] == s["sent"] + s["timeout"] + s["pending"])

  # 1-2) 대기 줄(64칸)보다 훨씬 많이 한꺼번에 -> 흐름 제어로 버리지 않아야 한다
  N = 2000
  b.can_rx = 0
  t0 = time.time()
  b.send_can(N, CAN_FD_BRS, 64)               # 보드가 못 받으면 여기서 PC 쪽 TCP 가 기다린다
  t_send = time.time() - t0
  end = time.time() + 10
  while b.can_rx < N and time.time() < end:
    b.collect(0.2, want_resp=False)
  t_all = time.time() - t0
  s = b.stat(clear=True)
  show(s)
  check("burst %d (flow control)" % N, s["queued"] == N and s["sent"] == N and b.can_rx == N
        and s["queue_full"] == 0 and s["rx_drop"] == 0 and s["lan_drop"] == 0,
        "(PC got %d, send took %.2f s, all back in %.2f s, %.0f frames/s)" % (b.can_rx, t_send, t_all, N / t_all))

  # 2) 모니터: 선에 누가 말하나
  check("open monitor", b.open(CAN_MONITOR, CAN_FD_BRS))
  b.can_rx = 0
  b.collect(2.0, want_resp=False)
  bus_frames = b.can_rx
  print("      bus traffic in 2 s : %d frame(s)" % bus_frames)

  # 3) 일반 모드 10 개 (선이 조용할 때만)
  if bus_frames == 0:
    b.stat(clear=True)
    check("open normal (classic 500K)", b.open(CAN_NORMAL, CAN_CLASSIC))
    b.send_can(10, CAN_CLASSIC, 8)
    b.collect(2.0, want_resp=False)
    s = b.stat()
    show(s)
    check("normal identity queued = sent + timeout + pending",
          s["queued"] == 10 and s["queued"] == s["sent"] + s["timeout"] + s["pending"])
    check("all settled (nothing stuck)", s["pending"] == 0)
    if s["timeout"] == 10:
      print("      -> no device ACKed: every frame given up after 100 ms (expected with nothing on the bus)")
    elif s["sent"] == 10:
      print("      -> another device ACKed all frames")
  else:
    print("      skip normal-mode send: bus is not quiet")

  b.cmd(CMD_CAN_CLOSE)
  print("\nALL OK" if fail == 0 else "\n%d FAIL" % fail)


if __name__ == "__main__":
  main()
