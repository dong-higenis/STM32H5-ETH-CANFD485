"""
tcp_port.py - COM 포트(serial.Serial)와 "겉모습이 똑같은" TCP 포트

  GUI 의 나머지 코드(CmdThread, CAN 탭, 펌웨어 다운로드 ...)는 COM 포트의
  open / close / read / write 만 쓴다. 이 클래스가 같은 이름의 함수를 TCP 로 구현해 주면
  나머지 코드는 하나도 안 고쳐도 된다.

  펌웨어의 cmd_tcp.c 가 cmd_uart.c 자리를 대신한 것과 같은 생각이다.
"""
import re
import select
import socket

DEFAULT_TCP_PORT = 5000

_NET_ADDR = re.compile(r"^\s*(\d{1,3}(?:\.\d{1,3}){3})(?::(\d{1,5}))?\s*$")


def isNetAddress(text):
  """'192.168.0.50' 또는 '192.168.0.50:5000' 이면 True"""
  m = _NET_ADDR.match(text or "")
  if not m:
    return False
  return all(0 <= int(x) <= 255 for x in m.group(1).split("."))


def splitNetAddress(text):
  m = _NET_ADDR.match(text)
  return m.group(1), int(m.group(2) or DEFAULT_TCP_PORT)


class TcpPort:
  def __init__(self, timeout=0.1):
    self.timeout       = timeout
    self.write_timeout = 5
    self.port          = ""      # "192.168.0.50" 또는 "192.168.0.50:5000"
    self.baudrate      = 0       # TCP 에서는 의미 없음 (COM 포트와 모양만 맞춤)
    self.sock          = None

  @property
  def is_open(self):
    return self.sock is not None

  def open(self):
    host, port = splitNetAddress(self.port)
    sock = socket.create_connection((host, port), timeout=2)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)   # 작은 봉투도 모으지 말고 바로 보내기
    sock.settimeout(self.write_timeout)   # 보내기 제한 시간. 받기는 read() 에서 select 로 따로 기다린다
    self.sock = sock

  def close(self):
    if self.sock is not None:
      try:
        self.sock.close()
      finally:
        self.sock = None

  def read(self, size=1):
    """받은 만큼 돌려준다. 시간 안에 없으면 b"" (COM 포트의 timeout 과 같은 동작)"""
    sock = self.sock
    if sock is None:
      raise IOError("tcp port is not open")
    ready, _, _ = select.select([sock], [], [], self.timeout)
    if not ready:
      return b""
    data = sock.recv(4096)
    if not data:
      # 보드가 연결을 끊었다
      self.close()
      raise IOError("tcp connection closed by board")
    return data

  def write(self, data):
    sock = self.sock
    if sock is None:
      raise IOError("tcp port is not open")
    sock.sendall(bytes(data))
    return len(data)

  # COM 포트에만 의미가 있는 함수들: 모양만 맞춘다
  def flush(self):        pass
  def flushInput(self):   pass
  def flushOutput(self):  pass
  def cancel_read(self):  pass
  def cancel_write(self): pass
