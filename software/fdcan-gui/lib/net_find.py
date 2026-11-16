"""
net_find.py - 랜에서 보드 찾기 (UDP 5001번)

  PC  -> 랜 전체 : "거기 보드 있어요?"   (Baram 봉투, cmd 0x0020)
  보드 -> PC     : "여기요! 이름/버전/IP" (Baram 봉투, net_find_t 90 바이트)

  펌웨어 쪽 약속: firmware/common/net_proto.h  (숫자와 구조가 같아야 한다)

  브로드캐스트는 PC 의 랜카드 하나로만 나가기 때문에,
  PC 에 있는 랜카드(유선, 와이파이 ...)마다 한 번씩 외친다.
"""
import select
import socket
import struct
import time

NET_UDP_FIND_PORT = 5001
NET_CMD_FIND      = 0x0020
NET_FIND_MAGIC    = 0x4E444946

PKT_TYPE_CMD  = 0x00
PKT_TYPE_RESP = 0x01

# net_find_t : magic, name[32], version[32], mac[6], ip[4], sn[4], gw[4], tcp_port, is_dhcp, is_busy
_FIND_FMT  = "<I32s32s6s4s4s4sHBB"
_FIND_SIZE = struct.calcsize(_FIND_FMT)     # 90


def _makePacket(pkt_type, cmd, data=b""):
  b = bytearray([0x02, 0xFD, pkt_type, cmd & 0xFF, cmd >> 8, 0, 0, len(data) & 0xFF, len(data) >> 8])
  b += data
  b.append((-sum(b)) & 0xFF)
  return bytes(b)


def parseReply(datagram):
  """보드 대답 한 장을 dict 로. 우리 보드 대답이 아니면 None"""
  if len(datagram) < 10 or datagram[0] != 0x02 or datagram[1] != 0xFD:
    return None
  length = datagram[7] | (datagram[8] << 8)
  if len(datagram) < 10 + length or (sum(datagram[:10 + length]) & 0xFF) != 0:
    return None
  pkt_type = datagram[2]
  cmd      = datagram[3] | (datagram[4] << 8)
  if pkt_type != PKT_TYPE_RESP or cmd != NET_CMD_FIND or length != _FIND_SIZE:
    return None

  magic, name, ver, mac, ip, sn, gw, port, is_dhcp, is_busy = struct.unpack(_FIND_FMT, datagram[9:9 + length])
  if magic != NET_FIND_MAGIC:
    return None
  return {
    "name":    name.split(b"\0")[0].decode(errors="replace"),
    "version": ver.split(b"\0")[0].decode(errors="replace"),
    "mac":     ":".join("%02X" % x for x in mac),
    "ip":      ".".join(str(x) for x in ip),
    "mask":    ".".join(str(x) for x in sn),
    "gw":      ".".join(str(x) for x in gw),
    "port":    port,
    "dhcp":    bool(is_dhcp),
    "busy":    bool(is_busy),
  }


def localIPv4():
  """이 PC 의 IPv4 주소 목록 (127.x 제외)"""
  ips = set()
  try:
    for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
      ips.add(info[4][0])
  except OSError:
    pass
  ips = [ip for ip in ips if not ip.startswith("127.")]
  return ips or ["0.0.0.0"]


def findBoards(timeout=0.6, retry=2):
  """찾은 보드 목록 [ {name, version, mac, ip, ...}, ... ]  (MAC 이 같으면 하나로)"""
  request = _makePacket(PKT_TYPE_CMD, NET_CMD_FIND)
  socks   = []
  boards  = {}

  for ip in localIPv4():
    try:
      s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
      s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
      s.bind((ip, 0))
      s.setblocking(False)
      socks.append(s)
    except OSError:
      pass

  try:
    for _ in range(retry):          # UDP 는 가끔 없어질 수 있으니 두 번 묻는다
      for s in socks:
        try:
          s.sendto(request, ("255.255.255.255", NET_UDP_FIND_PORT))
        except OSError:
          pass

      end = time.time() + timeout / retry
      while socks:
        left = end - time.time()
        if left <= 0:
          break
        ready, _, _ = select.select(socks, [], [], left)
        for s in ready:
          try:
            data, _ = s.recvfrom(1024)
          except OSError:
            continue
          board = parseReply(data)
          if board:
            boards[board["mac"]] = board
  finally:
    for s in socks:
      s.close()

  return sorted(boards.values(), key=lambda b: b["ip"])


if __name__ == "__main__":
  found = findBoards()
  if not found:
    print("no board found")
  for b in found:
    print("%-15s %-18s %-10s mac %s %s%s" % (b["ip"], b["name"], b["version"], b["mac"],
                                             "DHCP" if b["dhcp"] else "static", " (busy)" if b["busy"] else ""))
