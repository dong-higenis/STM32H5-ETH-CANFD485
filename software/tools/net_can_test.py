"""
net_can_test.py - 랜으로 CAN 기능 시험 (외부 CAN 장치 없이)

  사용:  python net_can_test.py 192.168.0.50

  보드 CAN 을 "루프백" 모드로 연다. 보드는 자기가 보낸 프레임을 칩 안에서 그대로 다시 받는다.
  그래서 PC -> 랜 -> 보드 -> CAN 칩 -> 보드 -> 랜 -> PC 전체 길을 외부 장치 없이 확인할 수 있다.
  (루프백에서는 트랜시버가 대기 상태라 실제 CAN 선에는 아무것도 나가지 않는다)

  시험 항목
    1) 잘못된 속도로 열기          -> 에러가 와야 정상
    2) 루프백으로 열기 (FD+BRS, 500K / 2M)
    3) 여러 종류 프레임 보내고 되돌아오는지 (Classic/FD/BRS, 표준/확장 ID, 0~64 바이트)
    4) 필터: 표준 ID 0x123 만 받기 -> 0x124 는 안 와야 함, 확장 ID 는 와야 함
    5) 연속 500 프레임 -> 몇 개 돌아오는지, 초당 몇 프레임인지
    6) 닫기
"""
import socket
import struct
import sys
import time

PORT = 5000
PKT_TYPE_CMD, PKT_TYPE_RESP, PKT_TYPE_CAN = 0x00, 0x01, 0x05
CMD_CAN_OPEN, CMD_CAN_CLOSE, CMD_CAN_DATA, CMD_CAN_SET_FILTER, CMD_CAN_GET_FILTER = 0x0110, 0x0111, 0x0112, 0x0114, 0x0115

CAN_NORMAL, CAN_MONITOR, CAN_LOOPBACK = 0, 1, 2
CAN_CLASSIC, CAN_FD_NO_BRS, CAN_FD_BRS = 0, 1, 2
CAN_STD, CAN_EXT = 0, 1
CAN_500K, CAN_1M, CAN_2M, CAN_5M = 3, 4, 5, 7
DLC_LEN = [0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64]


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
    self.can_rx = []

  def _pull(self, timeout):
    self.sock.settimeout(timeout)
    try:
      d = self.sock.recv(65536)
    except socket.timeout:
      return False
    if not d:
      raise ConnectionError("closed by board")
    self.rx += d
    return True

  def _next_packet(self):
    while len(self.rx) >= 2 and not (self.rx[0] == 0x02 and self.rx[1] == 0xFD):
      del self.rx[0]
    if len(self.rx) < 10:
      return None
    n = 9 + (self.rx[7] | self.rx[8] << 8) + 1
    if len(self.rx) < n:
      return None
    p = bytes(self.rx[:n]); del self.rx[:n]
    return {"type": p[2], "cmd": p[3] | p[4] << 8, "err": p[5] | p[6] << 8, "data": p[9:-1], "ok": sum(p) & 0xFF == 0}

  def _collect(self, timeout):
    """timeout 동안 들어온 것 정리: CAN 은 can_rx 로, 대답은 돌려줌"""
    end = time.time() + timeout
    while True:
      p = self._next_packet()
      if p:
        if p["type"] == PKT_TYPE_CAN:
          self.can_rx.append(parse_can(p["data"]))
          continue
        return p
      left = end - time.time()
      if left <= 0 or not self._pull(left):
        return None

  def request(self, cmd, data=b""):
    self.sock.sendall(make_packet(PKT_TYPE_CMD, cmd, data))
    return self._collect(1.0)

  def send_can(self, frame, id_type, dlc, can_id, data):
    length = DLC_LEN[dlc]
    body = struct.pack("<HBBBIB", 0, frame, id_type, dlc, can_id, length) + bytes(data).ljust(length, b"\0")[:length]
    self.sock.sendall(make_packet(PKT_TYPE_CAN, CMD_CAN_DATA, body))

  def wait_can(self, count, timeout):
    end = time.time() + timeout
    while len(self.can_rx) < count and time.time() < end:
      self._collect(0.05)
    return self.can_rx


def parse_can(d):
  ts, frame, id_type, dlc, can_id, length = struct.unpack_from("<HBBBIB", d, 0)
  return {"frame": frame, "id_type": id_type, "dlc": dlc, "id": can_id, "data": d[10:10 + length]}


def show(title, ok, detail=""):
  print("[%s] %-34s %s" % ("OK" if ok else "NG", title, detail))
  return ok


def main():
  if len(sys.argv) < 2:
    print("usage: python net_can_test.py <board ip>")
    return 1
  b = Board(sys.argv[1])
  ok_all = True

  r = b.request(CMD_CAN_OPEN, bytes([CAN_LOOPBACK, CAN_FD_BRS, CAN_5M, CAN_2M]))
  ok_all &= show("open with nominal 5M -> error", r is not None and r["err"] != 0, "err 0x%04X" % r["err"] if r else "no answer")

  r = b.request(CMD_CAN_OPEN, bytes([CAN_LOOPBACK, CAN_FD_BRS, CAN_500K, CAN_2M]))
  ok_all &= show("open loopback FD+BRS 500K/2M", r is not None and r["err"] == 0, "err 0x%04X" % r["err"] if r else "no answer")

  cases = [
    ("classic std 8B",    CAN_CLASSIC,   CAN_STD, 8,  0x123,     bytes(range(8))),
    ("classic std 0B",    CAN_CLASSIC,   CAN_STD, 0,  0x7FF,     b""),
    ("FD ext 12B",        CAN_FD_NO_BRS, CAN_EXT, 9,  0x1234567, bytes(range(12))),
    ("FD+BRS ext 64B",    CAN_FD_BRS,    CAN_EXT, 15, 0x1ABCDEF, bytes((i * 7) & 0xFF for i in range(64))),
    ("FD+BRS std 32B",    CAN_FD_BRS,    CAN_STD, 13, 0x055,     bytes(range(32))),
  ]
  for name, frame, id_type, dlc, can_id, data in cases:
    b.can_rx.clear()
    b.send_can(frame, id_type, dlc, can_id, data)
    rx = b.wait_can(1, 1.0)
    good = len(rx) == 1 and rx[0]["id"] == can_id and rx[0]["frame"] == frame and rx[0]["id_type"] == id_type \
           and rx[0]["dlc"] == dlc and rx[0]["data"] == bytes(data).ljust(DLC_LEN[dlc], b"\0")
    ok_all &= show("loopback " + name, good, "" if good else str(rx))

  # 필터: 표준 ID 0x123 만 (mask 0x7FF)
  r = b.request(CMD_CAN_SET_FILTER, struct.pack("<BBII", 0, CAN_STD, 0x123, 0x7FF))
  ok_all &= show("set filter std 0x123/0x7FF", r is not None and r["err"] == 0)
  b.can_rx.clear()
  b.send_can(CAN_CLASSIC, CAN_STD, 2, 0x124, b"\x01\x02")   # 걸러져야 함
  b.send_can(CAN_CLASSIC, CAN_STD, 2, 0x123, b"\x03\x04")   # 와야 함
  b.send_can(CAN_FD_BRS,  CAN_EXT, 2, 0x124, b"\x05\x06")   # 확장 ID 는 필터와 상관없이 와야 함
  rx = b.wait_can(2, 1.0); time.sleep(0.2); b._collect(0.1)
  ids = sorted((m["id_type"], m["id"]) for m in b.can_rx)
  ok_all &= show("filter result", ids == [(CAN_STD, 0x123), (CAN_EXT, 0x124)], str(ids))
  r = b.request(CMD_CAN_GET_FILTER)
  ok_all &= show("get filter", r is not None and r["data"] == struct.pack("<BBII", 0, CAN_STD, 0x123, 0x7FF))
  b.request(CMD_CAN_SET_FILTER, struct.pack("<BBII", 0, CAN_STD, 0, 0))   # 다시 모두 받기

  # 연속 전송 (PC 는 보내는 속도를 조금 조절: CAN 송신 FIFO 는 3칸뿐)
  b.can_rx.clear()
  n = 500
  t0 = time.time()
  for i in range(n):
    b.send_can(CAN_FD_BRS, CAN_EXT, 8, 0x100 + (i & 0xFF), struct.pack("<II", i, 0xA5A5A5A5))
    if i % 2 == 1:
      time.sleep(0.001)
  rx = b.wait_can(n, 5.0)
  dt = time.time() - t0
  seq = [struct.unpack_from("<I", m["data"])[0] for m in rx]
  in_order = seq == sorted(seq)
  ok_all &= show("burst %d frames" % n, len(rx) == n and in_order,
                 "got %d, %s, %.0f frames/s" % (len(rx), "in order" if in_order else "OUT OF ORDER", len(rx) / dt))

  r = b.request(CMD_CAN_CLOSE)
  ok_all &= show("close", r is not None and r["err"] == 0)
  b.sock.close()
  print("\nALL OK" if ok_all else "\nSOME TESTS FAILED")
  return 0 if ok_all else 1


if __name__ == "__main__":
  sys.exit(main())
