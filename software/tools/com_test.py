"""
com_test.py - J1 UART(COM 포트)로 봉투 시험 + 명령창이 같이 되는지 시험

  사용:  python com_test.py COM19

  J1 UART 하나를 사람(명령창)과 GUI(봉투)가 함께 쓴다.
  보드는 02 FD 로 시작하는 바이트만 봉투로 빼내고, 나머지는 명령창으로 보낸다.

  시험 항목
    1) 명령창: "info" 를 치면 글자로 대답이 오는지
    2) 봉투: 버전 읽기, LED, 모르는 명령(에러가 와야 정상)
    3) 대답 시간 (20번 평균)
    4) CAN 루프백: 열고, 여러 프레임 보내고, 다 돌아오는지
    5) 사람이 Enter 를 치면 보드가 CAN 을 닫는지 ("can info" -> open : no)
"""
import struct
import sys
import time

import serial

PKT_TYPE_CMD, PKT_TYPE_RESP, PKT_TYPE_CAN = 0x00, 0x01, 0x05
CMD_VERSION, CMD_LED = 0x0001, 0x0010
CMD_CAN_OPEN, CMD_CAN_CLOSE, CMD_CAN_DATA = 0x0110, 0x0111, 0x0112
ERR_CMD_NO_CMD = 0x0023
CAN_LOOPBACK, CAN_FD_BRS, CAN_STD, CAN_EXT = 2, 2, 0, 1
CAN_500K, CAN_2M = 3, 5
DLC_LEN = [0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64]

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
  def __init__(self, port):
    self.ser = serial.Serial(port, 115200, timeout=0.02)
    self.rx = bytearray()
    self.text = bytearray()       # 봉투가 아닌 바이트 (명령창 글자, 로그)
    self.can_rx = []

  def _pull(self):
    d = self.ser.read(4096)
    if d:
      self.rx += d

  def _next_packet(self):
    # 봉투 머리(02 FD) 앞의 바이트는 글자로 모아 둔다
    while len(self.rx) >= 1 and not (self.rx[0] == 0x02 and (len(self.rx) < 2 or self.rx[1] == 0xFD)):
      self.text.append(self.rx[0]); del self.rx[0]
    if len(self.rx) < 10:
      return None
    n = 10 + (self.rx[7] | self.rx[8] << 8)
    if len(self.rx) < n:
      return None
    p = bytes(self.rx[:n]); del self.rx[:n]
    return {"type": p[2], "cmd": p[3] | p[4] << 8, "err": p[5] | p[6] << 8, "data": p[9:-1], "ok": sum(p) & 0xFF == 0}

  def collect(self, timeout, want_resp=True):
    end = time.time() + timeout
    while time.time() < end:
      self._pull()
      while True:
        p = self._next_packet()
        if p is None:
          break
        if p["type"] == PKT_TYPE_CAN:
          self.can_rx.append(p["data"])
        elif want_resp:
          return p
    return None

  def cmd(self, cmd, data=b"", timeout=1.0):
    self.ser.write(make_packet(PKT_TYPE_CMD, cmd, data))
    return self.collect(timeout)

  def type_line(self, line, wait=0.5):
    self.text.clear()
    self.ser.write(line.encode() + b"\r")
    self.collect(wait, want_resp=False)
    return self.text.decode(errors="replace")


def can_body(frame, id_type, dlc, can_id, data):
  return struct.pack("<HBBBIB", 0, frame, id_type, dlc, can_id, len(data)) + data


def main():
  port = sys.argv[1] if len(sys.argv) > 1 else "COM19"
  b = Board(port)

  # 1) 명령창
  out = b.type_line("")
  out = b.type_line("info")
  check("CLI info", "app" in out, "(%d chars)" % len(out))

  # 2) 봉투
  p = b.cmd(CMD_VERSION)
  ok = p is not None and p["ok"] and p["err"] == 0
  name = ""
  if ok:
    # firm_ver_t x3 (boot, firm, update): magic 4 + version 32 + name 32 + addr 4
    name = p["data"][72 + 4 + 32: 72 + 4 + 64].split(b"\0")[0].decode(errors="replace")
    ver = p["data"][72 + 4: 72 + 36].split(b"\0")[0].decode(errors="replace")
    name = name + " " + ver
  check("packet VERSION", ok, name)

  p = b.cmd(CMD_LED)
  check("packet LED", p is not None and p["err"] == 0)

  p = b.cmd(0x7777)
  check("unknown cmd -> error", p is not None and p["err"] == ERR_CMD_NO_CMD, "err 0x%04X" % (p["err"] if p else 0))

  # 명령창 글자와 봉투가 섞여도 되는지: 글자 반쯤 치고 봉투, 그다음 Enter
  b.ser.write(b"inf")
  p = b.cmd(CMD_LED)
  out = b.type_line("o")
  check("packet in the middle of typing", p is not None and p["err"] == 0 and "app" in out)

  # 3) 대답 시간
  t = []
  for _ in range(20):
    s = time.time()
    p = b.cmd(CMD_LED)
    if p is not None:
      t.append((time.time() - s) * 1000)
  check("RTT x20", len(t) == 20, "avg %.1f ms, max %.1f ms" % (sum(t) / max(len(t), 1), max(t or [0])))

  # 4) CAN 루프백
  p = b.cmd(CMD_CAN_OPEN, bytes([CAN_LOOPBACK, CAN_FD_BRS, CAN_500K, CAN_2M]))
  check("CAN open loopback", p is not None and p["err"] == 0)

  b.can_rx.clear()
  sent = 0
  for i in range(50):
    dlc = i % 16
    data = bytes((i + k) & 0xFF for k in range(DLC_LEN[dlc]))
    id_type = CAN_EXT if i % 2 else CAN_STD
    can_id = (0x1000 + i) if id_type == CAN_EXT else (0x100 + i)
    b.ser.write(make_packet(PKT_TYPE_CAN, CMD_CAN_DATA, can_body(CAN_FD_BRS, id_type, dlc, can_id, data)))
    sent += 1
  b.collect(2.0, want_resp=False)
  check("CAN loopback frames", len(b.can_rx) == sent, "%d / %d" % (len(b.can_rx), sent))

  # 5) 사람이 Enter -> 보드가 CAN 을 닫는다
  out = b.type_line("")
  out = b.type_line("can info")
  check("Enter closes CAN", "open    : no" in out)

  b.ser.close()
  print("\nALL OK" if fail == 0 else "\n%d FAIL" % fail)


if __name__ == "__main__":
  main()
