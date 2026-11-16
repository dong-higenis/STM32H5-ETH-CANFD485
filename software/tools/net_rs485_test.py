"""
net_rs485_test.py - 랜으로 RS485 기능 시험

  사용:  python net_rs485_test.py 192.168.0.50 [--listen 5]

  시험 항목
    1) 잘못된 속도(100bps)로 열기 -> 에러가 와야 정상
    2) 115200bps 로 열기
    3) 1000 바이트 보내기 (보드가 RS485 선으로 내보냄)
    4) --listen N : N 초 동안 RS485 에서 들어오는 바이트를 화면에 보여준다
                    (USB-RS485 변환기 등으로 보드에 무언가를 보내 보면서 확인)
    5) 닫기

  주의: 이 보드는 보내는 동안 자기 수신기가 꺼진다(DE 와 /RE 가 묶여 있음).
        그래서 외부 장치 없이는 "받기"를 시험할 수 없다.
"""
import socket
import struct
import sys
import time

PORT = 5000
PKT_TYPE_CMD, PKT_TYPE_UART = 0x00, 0x06
CMD_RS485_OPEN, CMD_RS485_CLOSE, CMD_RS485_DATA = 0x0100, 0x0101, 0x0102


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
    self.uart_rx = bytearray()

  def _packets(self, timeout):
    end = time.time() + timeout
    while True:
      while len(self.rx) >= 2 and not (self.rx[0] == 0x02 and self.rx[1] == 0xFD):
        del self.rx[0]
      if len(self.rx) >= 10:
        n = 10 + (self.rx[7] | self.rx[8] << 8)
        if len(self.rx) >= n:
          p = bytes(self.rx[:n]); del self.rx[:n]
          yield {"type": p[2], "cmd": p[3] | p[4] << 8, "err": p[5] | p[6] << 8, "data": p[9:-1]}
          continue
      left = end - time.time()
      if left <= 0:
        return
      self.sock.settimeout(left)
      try:
        d = self.sock.recv(65536)
      except socket.timeout:
        return
      if not d:
        return
      self.rx += d

  def request(self, cmd, data=b""):
    self.sock.sendall(make_packet(PKT_TYPE_CMD, cmd, data))
    for p in self._packets(1.0):
      if p["type"] == PKT_TYPE_UART:
        self.uart_rx += p["data"]
        continue
      return p
    return None

  def listen(self, seconds):
    for p in self._packets(seconds):
      if p["type"] == PKT_TYPE_UART:
        self.uart_rx += p["data"]
        print("   rx %3d bytes : %s" % (len(p["data"]), p["data"][:32].hex(" ")))


def show(title, ok, detail=""):
  print("[%s] %-30s %s" % ("OK" if ok else "NG", title, detail))
  return ok


def main():
  if len(sys.argv) < 2:
    print("usage: python net_rs485_test.py <board ip> [--listen seconds]")
    return 1
  listen = int(sys.argv[sys.argv.index("--listen") + 1]) if "--listen" in sys.argv else 0
  b = Board(sys.argv[1])
  ok = True

  r = b.request(CMD_RS485_OPEN, struct.pack("<I", 100))
  ok &= show("open 100 bps -> error", r is not None and r["err"] != 0, "err 0x%04X" % r["err"] if r else "no answer")

  r = b.request(CMD_RS485_OPEN, struct.pack("<I", 115200))
  ok &= show("open 115200 bps", r is not None and r["err"] == 0)

  data = bytes(i & 0xFF for i in range(1000))
  for i in range(0, len(data), 250):
    b.sock.sendall(make_packet(PKT_TYPE_UART, CMD_RS485_DATA, data[i:i + 250]))
  time.sleep(0.2)   # 1000 바이트 @115200 = 약 87ms
  show("sent 1000 bytes to RS485", True, "(check 'rs485 info' tx on the board)")

  if listen:
    print("listening %d s ..." % listen)
    b.listen(listen)
    show("received from RS485", len(b.uart_rx) > 0, "%d bytes" % len(b.uart_rx))

  r = b.request(CMD_RS485_CLOSE)
  ok &= show("close", r is not None and r["err"] == 0)
  b.sock.close()
  print("\nALL OK" if ok else "\nSOME TESTS FAILED")
  return 0 if ok else 1


if __name__ == "__main__":
  sys.exit(main())
