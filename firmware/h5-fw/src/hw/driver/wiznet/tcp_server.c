/*
 * tcp_server.c  -  TCP 서버
 *
 *  W6300 소켓의 상태(getSn_SR)를 보고 할 일을 정한다. (Baram stm32h5-w6300 의 loopback_tcps 와 같은 흐름)
 *
 *    CLOSED      닫혀 있음     -> socket() 으로 연다
 *    INIT        열림          -> listen() 으로 "접속 기다림" 상태로
 *    LISTEN      기다리는 중   -> 할 일 없음 (PC 가 접속하면 칩이 알아서 ESTABLISHED 로 바꾼다)
 *    ESTABLISHED 연결됨        -> 받은 데이터를 꺼내 rx 버퍼에 담는다
 *    CLOSE_WAIT  PC 가 끊음     -> disconnect() 로 정리 -> 다시 CLOSED
 *
 *  받기/보내기는 WIZnet 의 recv()/send() 대신 칩 레지스터로 직접 한다.
 *
 *    WIZnet send() (W6300, non-block) 의 문제:
 *      데이터를 칩 송신 버퍼에 "먼저 써 넣고" 나서, 앞의 전송이 안 끝났으면 BUSY 를 돌려준다.
 *      BUSY 를 보고 다시 부르면 같은 데이터가 버퍼에 "한 번 더" 들어간다 -> 중복 전송.
 *
 *    그래서 순서를 직접 지킨다:
 *      1) 앞의 전송이 끝났나(SENDOK)  -> 아니면 1ms 씩 기다림 (최대 200ms)
 *      2) 송신 버퍼에 빈 공간이 있나  -> 아니면 1ms 씩 기다림 (최대 200ms)
 *      3) 써 넣고 SEND 명령
 *
 *  디버그: "eth debug on" 이면 주고받은 바이트를 로그로 보여준다.
 */
#include "tcp_server.h"

#ifdef _USE_HW_TCP_SERVER

#include "log.h"
#include "qbuffer.h"
#include "wiznet/wizchip_conf.h"
#include "wiznet/socket/socket.h"


#define TCP_RX_BUF_SIZE         2048
#define TCP_TX_TIMEOUT_MS       200
#define TCP_KEEPALIVE_5S        2           // 연결이 조용하면 10초(2 x 5초)마다 "살아있나?" 확인


static bool      is_init       = false;
static uint8_t   sn            = 0;
static uint16_t  listen_port   = 0;
static bool      is_connected  = false;
static bool      is_new_connect = false;
static bool      is_sending    = false;     // SEND 명령을 냈고 아직 SENDOK 를 못 받음
static volatile bool is_debug  = false;

static uint32_t  rx_bytes = 0;
static uint32_t  tx_bytes = 0;
static uint32_t  tx_fail  = 0;

static qbuffer_t rx_q;
static uint8_t   rx_q_buf[TCP_RX_BUF_SIZE];
static uint8_t   rx_tmp[512];


static void tcpServerOnConnect(void);
static void tcpServerOnDisconnect(void);
static void logBytes(const char *title, const uint8_t *p, uint32_t len);


bool tcpServerInit(uint8_t socket_num, uint16_t port)
{
  sn          = socket_num;
  listen_port = port;
  qbufferCreate(&rx_q, rx_q_buf, TCP_RX_BUF_SIZE);
  is_init = true;
  return true;
}

void tcpServerUpdate(void)
{
  uint8_t  status;
  uint16_t rx_len;
  uint32_t space;

  if (is_init != true)
    return;

  status = getSn_SR(sn);

  switch (status)
  {
    case SOCK_CLOSED:
      if (is_connected)
        tcpServerOnDisconnect();

      socket(sn, Sn_MR_TCP4, listen_port, 0);
      is_sending = false;
      break;

    case SOCK_INIT:
      listen(sn);
      break;

    case SOCK_ESTABLISHED:
      if (getSn_IR(sn) & Sn_IR_CON)       // "방금 연결됨" 표시
      {
        setSn_IR(sn, Sn_IR_CON);
        tcpServerOnConnect();
      }

      // 칩 안에 쌓인 데이터를 우리 rx 버퍼로 옮긴다 (버퍼에 들어갈 만큼만)
      rx_len = getSn_RX_RSR(sn);
      space  = (TCP_RX_BUF_SIZE - 1) - qbufferAvailable(&rx_q);
      rx_len = cmin(rx_len, (uint16_t)cmin(space, sizeof(rx_tmp)));
      if (rx_len > 0)
      {
        wiz_recv_data(sn, rx_tmp, rx_len);      // 읽고 RX_RD 를 옮긴다
        setSn_CR(sn, Sn_CR_RECV);               // "여기까지 읽었다"고 칩에 알린다
        while (getSn_CR(sn))
        {
        }
        qbufferWrite(&rx_q, rx_tmp, rx_len);
        rx_bytes += rx_len;
        if (is_debug)
          logBytes("tcp : rx", rx_tmp, rx_len);
      }
      break;

    case SOCK_CLOSE_WAIT:
      // PC 가 연결을 끊었다. 우리도 끊는다.
      disconnect(sn);
      tcpServerOnDisconnect();
      break;

    default:
      // LISTEN, SYNRECV, FIN_WAIT 등: 칩이 알아서 진행하는 중
      break;
  }
}

bool tcpServerIsConnected(void)
{
  return is_connected;
}

bool tcpServerIsNewConnect(void)
{
  bool ret = is_new_connect;
  is_new_connect = false;
  return ret;
}

// 지금 클라이언트 연결을 끊는다. 다음 tcpServerUpdate() 에서 CLOSED 를 보고 "끊김" 처리 후 다시 기다린다.
// (랜선이 빠져 IP 를 잃으면 이 연결은 어차피 못 쓴다. keepalive 가 알아챌 때까지 수십 초 기다리지 않는다)
void tcpServerDrop(void)
{
  if (is_init && is_connected)
  {
    logPrintf("tcp : drop (IP lost)\r\n");
    close(sn);
  }
}

uint32_t tcpServerAvailable(void)
{
  return qbufferAvailable(&rx_q);
}

uint8_t tcpServerRead(void)
{
  uint8_t data = 0;
  qbufferRead(&rx_q, &data, 1);
  return data;
}

void tcpServerFlush(void)
{
  qbufferFlush(&rx_q);
}

uint32_t tcpServerWrite(const uint8_t *p_data, uint32_t length)
{
  uint32_t pre_time;
  uint8_t  status;
  uint16_t chunk;
  uint32_t sent = 0;

  if (is_connected != true || length == 0)
    return 0;

  while (sent < length)
  {
    chunk = (uint16_t)cmin(length - sent, 1024U);

    // 1) 앞의 전송이 끝났나?
    pre_time = millis();
    while (is_sending)
    {
      uint8_t ir = getSn_IR(sn);

      if (ir & Sn_IR_SENDOK)
      {
        setSn_IR(sn, Sn_IR_SENDOK);
        is_sending = false;
      }
      else if ((ir & Sn_IR_TIMEOUT) || millis() - pre_time >= TCP_TX_TIMEOUT_MS)
      {
        tx_fail++;
        if (is_debug) logPrintf("tcp : tx fail (prev send not done, ir 0x%02X)\r\n", ir);
        return sent;
      }
      else
      {
        delay(1);
      }
    }

    // 2) 송신 버퍼에 빈 공간이 있나? 연결은 살아 있나?
    pre_time = millis();
    while (1)
    {
      status = getSn_SR(sn);
      if (status != SOCK_ESTABLISHED && status != SOCK_CLOSE_WAIT)
      {
        tx_fail++;
        if (is_debug) logPrintf("tcp : tx fail (socket status 0x%02X)\r\n", status);
        return sent;
      }
      if (getSn_TX_FSR(sn) >= chunk)
        break;
      if (millis() - pre_time >= TCP_TX_TIMEOUT_MS)
      {
        tx_fail++;
        if (is_debug) logPrintf("tcp : tx fail (buffer full)\r\n");
        return sent;
      }
      delay(1);
    }

    // 3) 써 넣고 보내기
    wiz_send_data(sn, (uint8_t *)&p_data[sent], chunk);   // 써 넣고 TX_WR 를 옮긴다
    setSn_CR(sn, Sn_CR_SEND);
    while (getSn_CR(sn))
    {
    }
    is_sending = true;

    if (is_debug)
      logBytes("tcp : tx", &p_data[sent], chunk);

    sent     += chunk;
    tx_bytes += chunk;
  }
  return sent;
}

void tcpServerSetDebug(bool enable)
{
  is_debug = enable;
}

void tcpServerGetStat(uint32_t *p_rx, uint32_t *p_tx, uint32_t *p_tx_fail)
{
  *p_rx      = rx_bytes;
  *p_tx      = tx_bytes;
  *p_tx_fail = tx_fail;
}

static void logBytes(const char *title, const uint8_t *p, uint32_t len)
{
  uint32_t show = cmin(len, 16U);

  logPrintf("[%6d] %s %d :", (int)millis(), title, (int)len);
  for (uint32_t i = 0; i < show; i++)
    logPrintf(" %02X", p[i]);
  logPrintf(len > show ? " ...\r\n" : "\r\n");
}


static void tcpServerOnConnect(void)
{
  uint8_t ip[4];

  getSn_DIPR(sn, ip);
  setSn_KPALVTR(sn, TCP_KEEPALIVE_5S);

  qbufferFlush(&rx_q);
  is_connected   = true;
  is_new_connect = true;
  is_sending     = false;

  logPrintf("tcp : connected from %d.%d.%d.%d:%d\r\n", ip[0], ip[1], ip[2], ip[3], (int)getSn_DPORT(sn));
}

static void tcpServerOnDisconnect(void)
{
  if (is_connected)
    logPrintf("tcp : disconnected\r\n");

  is_connected = false;
  qbufferFlush(&rx_q);
}

#endif
