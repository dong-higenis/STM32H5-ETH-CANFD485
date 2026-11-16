/*
 * net_task.c
 *
 *  랜 담당 태스크. W6300 을 만지는 일은 전부 이 태스크가 한다.
 *
 *    시작할 때 : W6300 초기화 (wiznetInit), TCP 서버 준비
 *    1ms 마다  : 랜선 확인, DHCP 진행      (wiznetUpdate)
 *                TCP 접속/수신 처리         (tcpServerUpdate)
 *                받은 패킷(명령) 처리 -> 응답 (cmdReceivePacket -> cmdBootProcess)
 *                "보드 찾기" 요청에 응답    (netFindUpdate, UDP 5001)
 *
 *  패킷이 들어오는 경로는 두 개다 (Step 10)
 *    cmd_tcp  : 랜 (TCP 5000)
 *    cmd_uart : J1 UART (COM 포트). cli 태스크의 RX hook 이 패킷만 골라 넘겨준다.
 *  어느 경로로 왔든 응답은 들어온 경로로 돌아간다.
 *  CAN/RS485 에서 받은 데이터는 "마지막으로 CAN/RS485 패킷을 보낸 경로"로 보낸다 (stream_cmd).
 *
 *  CLI 의 "eth" 명령은 cli 태스크에서 돌지만, 칩을 직접 만지지 않는다.
 *  상태는 wiznetGetInfo() 로 복사해 보고, 설정 변경은 wiznetRequestXXX() 로 요청만 한다.
 */
#include "net_task.h"
#include "cmd/cmd_tcp.h"
#include "cmd/cmd_uart.h"
#include "wdg_task.h"
#include "cmd/cmd_boot.h"
#include "cmd/cmd_can.h"
#include "cmd/cmd_rs485.h"
#include "wiznet/wizchip_conf.h"


static void netTask(void *arg);
static void netCmdProcess(cmd_t *p_cmd);
static void netStreamUpdate(void);
static void netStreamClose(const char *reason);
static void netSockDump(uint8_t sn);
static void cliEth(cli_args_t *args);
static bool parseIp(const char *str, uint8_t out[4]);


static cmd_t        cmd_tcp;
static cmd_driver_t cmd_tcp_driver;
static cmd_t        cmd_uart;
static cmd_driver_t cmd_uart_driver;
static cmd_t       *stream_cmd = NULL;    // CAN/RS485 데이터를 보낼 경로 (NULL = 아무도 안 씀)
static volatile int8_t sock_dump_req = -1;    // CLI 의 "eth sock n" 요청 (칩은 net 태스크가 읽는다)


bool netTaskInit(void)
{
  BaseType_t ret;

  cliAdd("eth", cliEth);
  cmdCanInit();
  cmdRs485Init();
  cmdUartInit();          // CLI 에 RX hook 등록 (cli 태스크가 UART 를 읽기 전에)

  ret = xTaskCreate(netTask, "net", 1024, NULL, TASK_PRIO_NORMAL, NULL);   // 스택 4KB
  return (ret == pdPASS);
}

static void netTask(void *arg)
{
  (void)arg;

  // wdg 태스크에 등록: 3초 넘게 루프를 못 돌면 "멈춤" (Step 11). 첫 aliveKick 은 칩 준비가 끝난 뒤 루프에서.
  int8_t alive_id = aliveAdd("net", 3000);

  // J1 UART 로 오는 패킷 (COM 포트 모드). 랜 칩이 없어도 이 경로는 쓸 수 있다.
  cmdUartInitDriver(&cmd_uart_driver);
  cmdInit(&cmd_uart, &cmd_uart_driver);
  cmdOpen(&cmd_uart);

  if (wiznetInit() != true)
  {
    // 랜 칩이 없으면 COM 포트 패킷만 처리한다.
    logPrintf("net : no W6300, COM port only\r\n");
    while (1)
    {
      aliveKick(alive_id);
      netCmdProcess(&cmd_uart);
      netStreamUpdate();
      delay(1);
    }
  }

  // TCP 서버(포트 5000) + 그 위에 Baram 명령 프로토콜
  tcpServerInit(HW_NET_SOCKET_CMD, HW_NET_CMD_PORT);
  // 위에서 tcp 서버를 이미 열었으므로 실질적인 cmd_tcp open함수에서 net open을 할 필요는 없다.

  cmdTcpInitDriver(&cmd_tcp_driver);
  cmdInit(&cmd_tcp, &cmd_tcp_driver);
  cmdOpen(&cmd_tcp);
  logPrintf("tcp : listening on port %d\r\n", HW_NET_CMD_PORT);

  // 보드 찾기(UDP 5001)에 대한 응답
  netFindInit(HW_NET_SOCKET_FIND, HW_NET_FIND_PORT);

  while (1)
  {
    aliveKick(alive_id);
    wiznetUpdate();
    if (tcpServerIsConnected() && !wiznetHasIp())
    {
      tcpServerDrop();      // 랜선이 빠져 IP 를 잃었다 -> 바로 끊는다 (CAN/RS485 도 닫힘) (Step 11)
    }
    tcpServerUpdate();
    if (tcpServerIsNewConnect())
    {
      cmdReset(&cmd_tcp);   // 새 클라이언트: 이전 클라이언트가 보내다 만 불완전한 패킷은 버린다
    }
    netCmdProcess(&cmd_tcp);
    netCmdProcess(&cmd_uart);
    netStreamUpdate();
    netFindUpdate();

    if (sock_dump_req >= 0)
    {
      netSockDump((uint8_t)sock_dump_req);
      sock_dump_req = -1;
    }
    delay(1);
  }
}

// 진단용: 소켓 하나의 칩 안 레지스터와 받기 버퍼 앞부분을 보여준다 (읽기만, 꺼내지는 않음)
static void netSockDump(uint8_t sn)
{
  uint16_t rx_rd = getSn_RX_RD(sn);
  uint8_t  peek[16];

  WIZCHIP_READ_BUF(((uint32_t)rx_rd << 8) + WIZCHIP_RXBUF_BLOCK(sn), peek, sizeof(peek));

  logPrintf("sock %d : MR 0x%02X  SR 0x%02X  IR 0x%02X  port %d\r\n",
            sn, getSn_MR(sn), getSn_SR(sn), getSn_IR(sn), (int)getSn_PORTR(sn));
  logPrintf("         RX: size %dKB  RSR %d  RD 0x%04X  WR 0x%04X\r\n",
            getSn_RX_BSR(sn), (int)getSn_RX_RSR(sn), rx_rd, getSn_RX_WR(sn));
  logPrintf("         TX: size %dKB  FSR %d  RD 0x%04X  WR 0x%04X\r\n",
            getSn_TX_BSR(sn), (int)getSn_TX_FSR(sn), getSn_TX_RD(sn), getSn_TX_WR(sn));
  logPrintf("         RX peek :");
  for (int i = 0; i < 16; i++)
    logPrintf(" %02X", peek[i]);
  logPrintf("\r\n");
}

// 받은 패킷을 하나씩 꺼내서 처리하고, 들어온 경로로 응답한다
//
// 흐름 제어: CAN TX 큐에 빈 슬롯이 없으면 패킷을 더 꺼내지 않는다.
//   남은 바이트는 TCP 수신 버퍼 -> W6300 수신 버퍼에 그대로 쌓이고, 다 차면 TCP 가 PC 에게
//   "잠깐 멈춰" 라고 알린다(윈도우 0). PC 가 CAN 보다 빨리 보내도 버리지 않고 기다리게 된다.
static void netCmdProcess(cmd_t *p_cmd)
{
  while (canMsgTxFree(_DEF_CAN1) > 0 && cmdReceivePacket(p_cmd) == true)
  {
    cmd_packet_t *p_packet = &p_cmd->packet;

    // 패킷이 깨졌으면(체크섬 틀림, 너무 김) 이유를 알려준다
    if (p_packet->err_code != OK)
    {
      cmdSendResp(p_cmd, p_packet->cmd, p_packet->err_code, NULL, 0);
      continue;
    }

    // 담당자에게 차례로 보여준다. "내 것"이면 true 를 돌려준다.
    if (cmdBootProcess(p_cmd)) continue;

    if (cmdCanProcess(p_cmd) || cmdRs485Process(p_cmd))
    {
      stream_cmd = p_cmd;   // CAN/RS485 를 쓰는 경로 = 받은 데이터를 보낼 경로
      continue;
    }

    // 아무도 처리하지 않은 "명령"이면 그렇다고 응답한다 (데이터 패킷은 응답하지 않는다)
    if (p_packet->type == PKT_TYPE_CMD)
    {
      cmdSendResp(p_cmd, p_packet->cmd, ERR_CMD_NO_CMD, NULL, 0);
    }
  }
}

// 받은 데이터(CAN 등)를 PC 로 보내고, PC 가 떠나면 정리한다
static void netStreamUpdate(void)
{
  static bool was_connected = false;
  bool        connected     = tcpServerIsConnected();

  // TCP 클라이언트 연결이 끊겼다
  if (was_connected && !connected && stream_cmd == &cmd_tcp)
  {
    netStreamClose("PC disconnected");
  }
  was_connected = connected;

  // COM 포트 쪽 CLI 에서 Enter 가 들어왔다 = GUI 가 아니라 터미널이 붙어 있다
  if (cmdUartTakeHumanEnter() && stream_cmd == &cmd_uart)
  {
    netStreamClose("terminal input");
  }

  if (stream_cmd != NULL)
  {
    cmdCanUpdate(stream_cmd);
    cmdRs485Update(stream_cmd);
  }
}

// CAN, RS485 를 닫고 트랜시버를 대기로. 데이터를 보낼 경로도 없앤다.
static void netStreamClose(const char *reason)
{
  logPrintf("net : %s -> close CAN/RS485\r\n", reason);
  cmdCanClose();
  cmdRs485Close();
  stream_cmd = NULL;
}


// eth info / eth dhcp / eth static <ip> <mask> <gw>
static void cliEth(cli_args_t *args)
{
  wiznet_info_t info;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    wiznetGetInfo(&info);

    cliPrintf("chip  : %s\n", info.is_chip ? "W6300 OK" : "not found");
    cliPrintf("link  : %s\n", info.is_link ? "UP" : "DOWN");
    cliPrintf("mode  : %s\n", info.is_dhcp ? "DHCP" : "static");
    cliPrintf("ip ok : %s\n", info.is_ip ? "yes" : "no");
    cliPrintf("mac   : %02X:%02X:%02X:%02X:%02X:%02X\n",
              info.mac[0], info.mac[1], info.mac[2], info.mac[3], info.mac[4], info.mac[5]);
    cliPrintf("ip    : %d.%d.%d.%d\n", info.ip[0], info.ip[1], info.ip[2], info.ip[3]);
    cliPrintf("mask  : %d.%d.%d.%d\n", info.sn[0], info.sn[1], info.sn[2], info.sn[3]);
    cliPrintf("gw    : %d.%d.%d.%d\n", info.gw[0], info.gw[1], info.gw[2], info.gw[3]);
    if (info.is_dhcp)
    {
      cliPrintf("dns   : %d.%d.%d.%d\n", info.dns[0], info.dns[1], info.dns[2], info.dns[3]);
      cliPrintf("lease : %d s\n", (int)info.lease_time);
    }
    cliPrintf("tcp   : port %d, %s\n", HW_NET_CMD_PORT, tcpServerIsConnected() ? "connected" : "waiting");
    cliPrintf("find  : udp %d, answered %d, dropped %d\n",
              HW_NET_FIND_PORT, (int)netFindGetAnswerCount(), (int)netFindGetDropCount());
    cliPrintf("qspi  : error %d\n", (int)info.qspi_error);
    cliPrintf("data  : CAN/RS485 -> %s\n",
              stream_cmd == &cmd_tcp ? "LAN" : stream_cmd == &cmd_uart ? "COM" : "(none)");
    return;
  }

  if (args->argc == 1 && args->isStr(0, "dhcp"))
  {
    wiznetRequestDHCP();
    cliPrintf("eth : DHCP requested\n");
    return;
  }

  if (args->argc == 4 && args->isStr(0, "static"))
  {
    uint8_t ip[4], sn[4], gw[4];

    if (parseIp(args->getStr(1), ip) && parseIp(args->getStr(2), sn) && parseIp(args->getStr(3), gw))
    {
      wiznetRequestStatic(ip, sn, gw);
      cliPrintf("eth : static IP requested\n");
    }
    else
    {
      cliPrintf("wrong address. ex) eth static 192.168.0.50 255.255.255.0 192.168.0.1\n");
    }
    return;
  }

  // 진단: TCP 로 주고받는 바이트를 로그로 보기
  if (args->argc == 2 && args->isStr(0, "debug"))
  {
    bool on = args->isStr(1, "on");
    tcpServerSetDebug(on);
    cliPrintf("eth debug : %s\n", on ? "on" : "off");
    return;
  }

  // 진단: 소켓 하나의 칩 안 상태 보기 (net 태스크가 읽어서 로그로 출력)
  if (args->argc == 2 && args->isStr(0, "sock"))
  {
    int n = args->getData(1);
    if (n >= 0 && n < 8)
    {
      sock_dump_req = (int8_t)n;
      delay(50);              // net 태스크가 출력할 시간
    }
    return;
  }

  cliPrintf("eth info\n");
  cliPrintf("eth dhcp\n");
  cliPrintf("eth static <ip> <mask> <gateway>\n");
  cliPrintf("    ex) eth static 192.168.0.50 255.255.255.0 192.168.0.1\n");
  cliPrintf("eth debug on|off      (show TCP bytes)\n");
  cliPrintf("eth sock <0~7>        (show socket registers)\n");
}

// "192.168.0.50" -> {192, 168, 0, 50}
static bool parseIp(const char *str, uint8_t out[4])
{
  int  value = 0;
  int  index = 0;
  bool has_digit = false;

  for (const char *p = str; ; p++)
  {
    if (*p >= '0' && *p <= '9')
    {
      value = value * 10 + (*p - '0');
      if (value > 255) return false;
      has_digit = true;
    }
    else if (*p == '.' || *p == 0)
    {
      if (!has_digit || index >= 4) return false;
      out[index++] = (uint8_t)value;
      value = 0;
      has_digit = false;
      if (*p == 0) break;
    }
    else
    {
      return false;
    }
  }
  return (index == 4);
}
