/*
 * net_find.c  -  "보드 찾기" 응답 (UDP 5001번)
 *
 *  PC 가 랜 전체에 보드 찾기 요청을 브로드캐스트하면 (UDP)
 *  보드가 이름, IP 등을 담아 응답한다 (net_find_t).
 *
 *  왜 이것만 UDP 인가?
 *    브로드캐스트는 TCP 로는 할 수 없다.
 *    TCP 는 상대 주소를 알아야 연결할 수 있는데, 지금은 그 주소를 찾는 중이기 때문이다.
 *    요청 하나, 응답 하나로 끝나서 UDP 의 패킷 손실도 문제가 안 된다. (PC 가 다시 요청하면 된다)
 *
 *  응답은 어디로 보내나?
 *    PC 와 같은 서브넷이면 PC 에게 바로(유니캐스트), 아니면 랜 전체(브로드캐스트)로 보낸다.
 *    -> 보드 IP 를 실수로 다른 서브넷으로 정해 놓았어도 PC 가 찾을 수 있다.
 */
#include "net_find.h"

#ifdef _USE_HW_NET_FIND

#include "cmd.h"
#include "log.h"
#include "wiznet.h"
#include "tcp_server.h"
#include "wiznet/wizchip_conf.h"
#include "wiznet/socket/socket.h"


extern const firm_ver_t firm_ver;     // hw.c 의 앱 버전 정보


#define FIND_RX_BUF_SIZE      512
#define FIND_UDP_HEAD_SIZE    8         // W6300 UDP(IPv4) 수신 헤더: [정보 2][보낸 IP 4][보낸 포트 2]


static bool     is_init = false;
static uint8_t  sn      = 0;
static uint16_t port    = 0;
static uint8_t  rx_raw[FIND_RX_BUF_SIZE];
static uint8_t  tx_buf[sizeof(net_find_t) + 10];

static uint32_t answer_count  = 0;      // 응답한 횟수
static uint32_t drop_count    = 0;      // 이상해서 버린 횟수
static uint32_t log_time      = 0;
static uint32_t log_skipped   = 0;


static bool isSameSubnet(const uint8_t a[4], const uint8_t b[4], const uint8_t mask[4]);
static void handleRequest(const uint8_t pc_ip[4], uint16_t pc_port, const uint8_t *p_data, uint16_t length);
static void dropAll(uint16_t length, const char *reason);


bool netFindInit(uint8_t socket_num, uint16_t udp_port)
{
  sn      = socket_num;
  port    = udp_port;
  is_init = true;
  return true;
}

/*
 * 받기를 WIZnet 의 recvfrom() 대신 직접 한다.
 *
 *   recvfrom() 은 패킷 1개를 [헤더 2] -> [IP 4] -> [포트 2] -> [내용] 순서로 여러 번 나눠 읽는다.
 *   요청이 여러 개 쌓였을 때 한 번이라도 읽는 위치가 어긋나면, 다음 패킷의 내용을 헤더로 읽고
 *   그 뒤로도 계속 어긋난 채로 읽는다 -> "find : answered 2.253.0.32" 가 끝없이 찍혔다.
 *   (2.253.0.32 = 02 FD 00 20 = Baram 패킷의 앞 4바이트)
 *
 *   그래서: 쌓인 것을 "한 번에 전부" 꺼낸 뒤, 헤더를 직접 하나씩 확인한다.
 *           조금이라도 이상하면 그 뒤는 전부 버린다. -> 어긋난 채로 계속 읽는 일이 없다.
 */
void netFindUpdate(void)
{
  uint16_t rsr;
  uint16_t off = 0;

  if (is_init != true)
    return;

  // 소켓이 UDP 로 열려 있지 않으면 연다
  if (getSn_SR(sn) != SOCK_UDP)
  {
    if (socket(sn, Sn_MR_UDP4, port, 0) == sn)
    {
      uint8_t io_mode = SOCK_IO_NONBLOCK;
      ctlsocket(sn, CS_SET_IOMODE, &io_mode);
    }
    return;
  }

  rsr = getSn_RX_RSR(sn);
  if (rsr == 0)
    return;

  if (rsr > sizeof(rx_raw))
  {
    dropAll(rsr, "too much data");          // 요청은 18바이트 정도. 이렇게 많을 리 없다
    return;
  }

  // 쌓인 것 전부 한 번에 꺼내기
  wiz_recv_data(sn, rx_raw, rsr);
  setSn_CR(sn, Sn_CR_RECV);
  while (getSn_CR(sn))
  {
  }

  // 패킷 1개씩 헤더를 확인하며 처리
  while (off + FIND_UDP_HEAD_SIZE <= rsr)
  {
    const uint8_t *p     = &rx_raw[off];
    uint16_t       len   = ((p[0] & 0x07) << 8) | p[1];    // [정보 2] 의 아래 11비트 = 내용 길이
    bool           is_v6 = (p[0] & 0x80) != 0;
    uint16_t       pc_port = (p[6] << 8) | p[7];

    if (is_v6 || len == 0 || off + FIND_UDP_HEAD_SIZE + len > rsr || pc_port == 0)
    {
      drop_count++;
      logPrintf("find : bad header at %d, dropped rest\r\n", off);
      break;                                // 이 뒤는 믿을 수 없으니 버린다 (이미 칩에서는 꺼냈음)
    }

    handleRequest(&p[2], pc_port, &p[FIND_UDP_HEAD_SIZE], len);
    off += FIND_UDP_HEAD_SIZE + len;
  }
}

uint32_t netFindGetAnswerCount(void) { return answer_count; }
uint32_t netFindGetDropCount(void)   { return drop_count; }


static void handleRequest(const uint8_t pc_ip[4], uint16_t pc_port, const uint8_t *p_data, uint16_t length)
{
  wiznet_info_t info;
  cmd_packet_t  packet;
  net_find_t    reply;
  uint32_t      tx_len;
  uint8_t       broadcast[4] = {255, 255, 255, 255};
  uint8_t       ip[4];

  memcpy(ip, pc_ip, 4);

  // Baram 패킷이고, "보드 찾기" 요청인가?
  if (cmdParsePacket(p_data, length, &packet) != true)
    return;
  if (packet.type != PKT_TYPE_CMD || packet.cmd != NET_CMD_FIND)
    return;

  // IP 가 없으면 응답할 수 없다 (송신 주소가 0.0.0.0 이 되므로)
  wiznetGetInfo(&info);
  if (info.is_ip != true)
    return;

  // 내가 보낸 것이 돌아온 경우는 무시
  if (memcmp(ip, info.ip, 4) == 0)
    return;

  netFindMakeReply(&reply);
  tx_len = cmdMakePacket(tx_buf, sizeof(tx_buf), PKT_TYPE_RESP, NET_CMD_FIND, OK, (uint8_t *)&reply, sizeof(reply));

  if (isSameSubnet(ip, info.ip, info.sn))
    sendto_W6x00(sn, tx_buf, (uint16_t)tx_len, ip, pc_port, 4);
  else
    sendto_W6x00(sn, tx_buf, (uint16_t)tx_len, broadcast, pc_port, 4);

  answer_count++;

  // 로그는 1초에 한 줄만 (PC 는 랜카드마다 두 번씩 묻기 때문에 몰려서 온다)
  if (millis() - log_time >= 1000)
  {
    logPrintf("find : answered %d.%d.%d.%d:%d", ip[0], ip[1], ip[2], ip[3], pc_port);
    if (log_skipped > 0)
      logPrintf(" (+%d more)", (int)log_skipped);
    logPrintf("\r\n");
    log_time    = millis();
    log_skipped = 0;
  }
  else
  {
    log_skipped++;
  }
}

static void dropAll(uint16_t length, const char *reason)
{
  wiz_recv_ignore(sn, length);
  setSn_CR(sn, Sn_CR_RECV);
  while (getSn_CR(sn))
  {
  }
  drop_count++;
  logPrintf("find : dropped %d bytes (%s)\r\n", length, reason);
}

void netFindMakeReply(net_find_t *p_reply)
{
  wiznet_info_t info;

  wiznetGetInfo(&info);

  memset(p_reply, 0, sizeof(net_find_t));
  p_reply->magic = NET_FIND_MAGIC;
  strncpy(p_reply->name,    firm_ver.name_str,    sizeof(p_reply->name) - 1);
  strncpy(p_reply->version, firm_ver.version_str, sizeof(p_reply->version) - 1);
  memcpy(p_reply->mac, info.mac, 6);
  memcpy(p_reply->ip,  info.ip,  4);
  memcpy(p_reply->sn,  info.sn,  4);
  memcpy(p_reply->gw,  info.gw,  4);
  p_reply->tcp_port = NET_TCP_CMD_PORT;
  p_reply->is_dhcp  = info.is_dhcp ? 1 : 0;
  p_reply->is_busy  = tcpServerIsConnected() ? 1 : 0;
}

static bool isSameSubnet(const uint8_t a[4], const uint8_t b[4], const uint8_t mask[4])
{
  for (int i = 0; i < 4; i++)
  {
    if ((a[i] & mask[i]) != (b[i] & mask[i]))
      return false;
  }
  return true;
}

#endif
