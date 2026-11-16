#include "cmd_rs485.h"


/**
 * @brief RS485 명령 처리 (GUI 의 RS485 탭)
 *
 *  명령 번호와 패킷 내용은 Baram G4 펌웨어 / GUI(lib/cmd_rs485.py)와 같다.
 *
 *    CMD_RS485_OPEN   0x0100  [baud 4바이트]               -> 응답
 *    CMD_RS485_CLOSE  0x0101                               -> 응답
 *    CMD_RS485_DATA   0x0102  (PKT_TYPE_UART, 양방향, 응답 없음)  내용 = 바이트 그대로
 *
 *  RS485 에서 받은 바이트는 1ms 마다 모아서 PC 로 보낸다. (패킷 하나에 최대 256 바이트)
 */


#define CMD_RS485_OPEN        0x0100
#define CMD_RS485_CLOSE       0x0101
#define CMD_RS485_DATA        0x0102

#define RS485_BAUD_MIN        1200
#define RS485_BAUD_MAX        1000000
#define RS485_PKT_DATA_MAX    256
#define RS485_STREAM_BUF_SIZE 1024


static const uint8_t ch = HW_UART_CH_RS485;
static uint8_t       stream_buf[RS485_STREAM_BUF_SIZE];


static void cliRs485(cli_args_t *args);


/**
 * @brief CLI 에 "rs485" 명령을 등록한다.
 */
void cmdRs485Init(void)
{
  cliAdd("rs485", cliRs485);
}

/**
 * @brief RS485 명령/데이터 패킷이면 처리하고 true, 아니면 false
 */
bool cmdRs485Process(cmd_t *p_cmd)
{
  cmd_packet_t *p_packet = &p_cmd->packet;

  // PC -> RS485 (응답 없음)
  if (p_packet->type == PKT_TYPE_UART && p_packet->cmd == CMD_RS485_DATA)
  {
    if (uartIsOpen(ch))
      uartWrite(ch, p_packet->data, p_packet->length);
    return true;
  }
  if (p_packet->type != PKT_TYPE_CMD)
    return false;

  if (p_packet->cmd == CMD_RS485_OPEN)
  {
    uint32_t baud = 0;
    bool     ret  = false;

    if (p_packet->length >= 4)
    {
      memcpy(&baud, p_packet->data, 4);
      if (baud >= RS485_BAUD_MIN && baud <= RS485_BAUD_MAX)
        ret = uartOpen(ch, baud);
    }
    logPrintf("rs485 : open %d bps -> %s\r\n", (int)baud, ret ? "OK" : "NG");
    cmdSendResp(p_cmd, p_packet->cmd, ret ? OK : ERR_INVAILD_INDEX, NULL, 0);
    return true;
  }

  if (p_packet->cmd == CMD_RS485_CLOSE)
  {
    uartClose(ch);
    logPrintf("rs485 : close\r\n");
    cmdSendResp(p_cmd, p_packet->cmd, OK, NULL, 0);
    return true;
  }

  return false;
}

/**
 * @brief RS485 에서 받은 바이트를 256 바이트씩 패킷에 담아, 모아서 한 번에 PC 로 보낸다.
 */
void cmdRs485Update(cmd_t *p_cmd)
{
  cmd_driver_t *p_driver = p_cmd->p_driver;
  uint8_t       data[RS485_PKT_DATA_MAX];
  uint32_t      used = 0;

  if (uartIsOpen(ch) != true)
    return;

  while (uartAvailable(ch) > 0 && used + RS485_PKT_DATA_MAX + 10 <= sizeof(stream_buf))
  {
    uint32_t len = cmin(uartAvailable(ch), (uint32_t)RS485_PKT_DATA_MAX);

    for (uint32_t i = 0; i < len; i++)
      data[i] = uartRead(ch);

    used += cmdMakePacket(&stream_buf[used], sizeof(stream_buf) - used,
                          PKT_TYPE_UART, CMD_RS485_DATA, OK, data, len);
  }

  if (used > 0 && p_cmd->is_open)
  {
    p_driver->write(p_driver->args, stream_buf, used);
  }
}

/**
 * @brief PC 연결이 끊기면 RS485 를 닫는다.
 */
void cmdRs485Close(void)
{
  if (uartIsOpen(ch))
  {
    uartClose(ch);
    logPrintf("rs485 : closed (PC disconnected)\r\n");
  }
}

/**
 * @brief CLI "rs485 info" : 보드에서 RS485 상태 보기
 */
static void cliRs485(cli_args_t *args)
{
  uart_stat_t stat;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    uartGetStat(ch, &stat);
    cliPrintf("open    : %s", uartIsOpen(ch) ? "yes" : "no");
    if (uartIsOpen(ch))
      cliPrintf(" (%d bps)", (int)uartGetBaud(ch));
    cliPrintf("\n");
    cliPrintf("rx      : %d bytes (overrun %d, error %d)\n", (int)stat.rx_count, (int)stat.rx_overrun, (int)stat.rx_error);
    cliPrintf("tx      : %d bytes (dropped %d)\n", (int)stat.tx_count, (int)stat.tx_drop);
    return;
  }
  cliPrintf("rs485 info\n");
}
