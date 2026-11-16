#include "cmd_can.h"


/**
 * @brief CAN 명령 처리 (GUI 의 CAN 탭)
 *
 *  명령 번호와 패킷 내용은 Baram G4 펌웨어 / GUI(lib/cmd_can.py)와 같다.
 *
 *    CMD_CAN_OPEN       0x0110  [mode, frame, baud, baud_data]           -> 응답
 *    CMD_CAN_CLOSE      0x0111                                           -> 응답
 *    CMD_CAN_DATA       0x0112  (PKT_TYPE_CAN, 양방향, 응답 없음)
 *                               [시각 2][frame][id_type][dlc][id 4][length][data ...]
 *    CMD_CAN_SET_FILTER 0x0114  [type, id_type, id1 4, id2 4]            -> 응답
 *    CMD_CAN_GET_FILTER 0x0115                                           -> 응답 [type, id_type, id1, id2]
 *    CMD_CAN_GET_STAT   0x0116  [clear(선택, 1 = 읽은 뒤 0 으로)]         -> 응답 [카운터 13개, 각 4바이트]
 *                               (H5 에만 있음. 데이터 손실 시험용. 순서는 canCmdGetStat() 참고)
 *
 *  CAN 에서 받은 프레임은 여러 개를 모아서 TCP 로 한 번에 보낸다.
 *  (프레임마다 따로 보내면 랜 쪽 부담이 커진다)
 */


#define CMD_CAN_OPEN          0x0110
#define CMD_CAN_CLOSE         0x0111
#define CMD_CAN_DATA          0x0112
#define CMD_CAN_SET_FILTER    0x0114
#define CMD_CAN_GET_FILTER    0x0115
#define CMD_CAN_GET_STAT      0x0116

#define CAN_STREAM_BUF_SIZE   1024          // 한 번에 TCP 로 보낼 묶음 크기
#define CAN_PKT_MAX           (10 + 10 + 64) // 패킷 헤더 9 + 체크섬 1 + CAN 헤더 10 + 데이터 64


static uint8_t         can_ch       = _DEF_CAN1;
static CanFilterType_t filter_type  = CAN_ID_MASK;
static CanIdType_t     filter_id_type = CAN_STD;
static uint32_t        filter_id1   = 0;
static uint32_t        filter_id2   = 0;
static uint8_t         stream_buf[CAN_STREAM_BUF_SIZE];
static uint32_t        stream_drop  = 0;      // CAN 에서 받았지만 PC 로 못 보내고 잃은 프레임 수


static void canCmdOpen(cmd_t *p_cmd);
static void canCmdClose(cmd_t *p_cmd);
static void canCmdData(cmd_t *p_cmd);
static void canCmdSetFilter(cmd_t *p_cmd);
static void canCmdGetFilter(cmd_t *p_cmd);
static void canCmdGetStat(cmd_t *p_cmd);
static void cliCan(cli_args_t *args);


/**
 * @brief CLI 에 "can" 명령을 등록한다. (RTOS 시작 전에)
 */
void cmdCanInit(void)
{
  cliAdd("can", cliCan);
}

/**
 * @brief CLI "can" 명령
 *        can info  : 보드에서 CAN 상태 보기 (손실 카운터 포함)
 *        can clear : 카운터를 0 으로
 */
static void cliCan(cli_args_t *args)
{
  can_info_t info;
  static const char *mode_str[] = {"normal", "monitor", "loopback"};

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    canGetInfo(can_ch, &info);
    cliPrintf("open    : %s", info.is_open ? "yes" : "no");
    if (info.is_open)
      cliPrintf(" (%s)", mode_str[info.mode]);
    cliPrintf("\n");
    cliPrintf("PC->CAN : queued %d = sent %d + timeout %d + pending %d,  queue full %d\n",
              (int)info.tx_count, (int)info.tx_ok, (int)info.tx_timeout, (int)info.tx_pending, (int)info.tx_fail);
    cliPrintf("CAN->PC : rx %d,  chip FIFO lost %d (times),  queue full %d,  LAN send fail %d\n",
              (int)info.rx_count, (int)info.rx_fifo_lost, (int)info.rx_drop, (int)stream_drop);
    cliPrintf("bus     : TEC %d, REC %d%s, last error %d, bus-off %d times\n",
              (int)info.tec, (int)info.rec, info.bus_off ? ", BUS-OFF" : "", (int)info.last_error,
              (int)info.bus_off_count);
    cliPrintf("filter  : %s %s id1 0x%X id2 0x%X\n",
              filter_type == CAN_ID_MASK ? "mask" : "range", filter_id_type == CAN_STD ? "std" : "ext",
              (unsigned)filter_id1, (unsigned)filter_id2);
    return;
  }
  if (args->argc == 1 && args->isStr(0, "clear"))
  {
    canClearCount(can_ch);
    stream_drop = 0;
    cliPrintf("can : count cleared\n");
    return;
  }
  cliPrintf("can info\n");
  cliPrintf("can clear\n");
}


/**
 * @brief CAN 명령/데이터 패킷이면 처리하고 true, 아니면 false
 */
bool cmdCanProcess(cmd_t *p_cmd)
{
  cmd_packet_t *p_packet = &p_cmd->packet;

  if (p_packet->type == PKT_TYPE_CAN && p_packet->cmd == CMD_CAN_DATA)
  {
    canCmdData(p_cmd);
    return true;
  }
  if (p_packet->type != PKT_TYPE_CMD)
    return false;

  switch (p_packet->cmd)
  {
    case CMD_CAN_OPEN:       canCmdOpen(p_cmd);      return true;
    case CMD_CAN_CLOSE:      canCmdClose(p_cmd);     return true;
    case CMD_CAN_SET_FILTER: canCmdSetFilter(p_cmd); return true;
    case CMD_CAN_GET_FILTER: canCmdGetFilter(p_cmd); return true;
    case CMD_CAN_GET_STAT:   canCmdGetStat(p_cmd);   return true;
    default:                 return false;
  }
}

/**
 * @brief CAN 에서 받은 프레임을 패킷으로 만들어 PC 로 보낸다. (net 태스크가 1ms 마다 부름)
 *        끝까지 못 보낸 패킷 수는 stream_drop 에 더한다.
 */
void cmdCanUpdate(cmd_t *p_cmd)
{
  cmd_driver_t *p_driver = p_cmd->p_driver;
  can_msg_t     msg;
  uint8_t       body[10 + 64];
  uint32_t      used = 0;
  uint32_t      frame_end[CAN_STREAM_BUF_SIZE / 30];   // 패킷마다 끝 위치. 한 번에 최대 34개 (1ms 마다라 충분)
  uint32_t      frames = 0;

  if (canIsOpen(can_ch) != true)
    return;

  canUpdate(can_ch);        // 오래 못 보낸 프레임 포기 (받아 주는 장치가 없을 때)

  while (canMsgAvailable(can_ch) > 0 && used + CAN_PKT_MAX <= sizeof(stream_buf) && frames < ARRAY_COUNT(frame_end))
  {
    uint32_t index = 0;

    canMsgRead(can_ch, &msg);

    body[index++] = (uint8_t)(msg.timestamp >> 0);   // 시각 (ms, 아래 16비트)
    body[index++] = (uint8_t)(msg.timestamp >> 8);
    body[index++] = (uint8_t)msg.frame;
    body[index++] = (uint8_t)msg.id_type;
    body[index++] = (uint8_t)msg.dlc;
    body[index++] = (uint8_t)(msg.id >> 0);
    body[index++] = (uint8_t)(msg.id >> 8);
    body[index++] = (uint8_t)(msg.id >> 16);
    body[index++] = (uint8_t)(msg.id >> 24);
    body[index++] = (uint8_t)msg.length;
    memcpy(&body[index], msg.data, msg.length);
    index += msg.length;

    used += cmdMakePacket(&stream_buf[used], sizeof(stream_buf) - used,
                          PKT_TYPE_CAN, CMD_CAN_DATA, OK, body, index);
    frame_end[frames++] = used;
  }

  if (used > 0)
  {
    uint32_t sent = p_cmd->is_open ? p_driver->write(p_driver->args, stream_buf, used) : 0;   // 모아서 한 번에

    // 다 못 보냈으면, 끝까지 못 나간 패킷 수 = 잃은 프레임 수
    if (sent < used)
    {
      for (uint32_t i = 0; i < frames; i++)
      {
        if (frame_end[i] > sent)
          stream_drop++;
      }
    }
  }
}

/**
 * @brief 손실 시험용 카운터 13개를 보낸다. (각 4바이트, little endian)
 *          0 rx_count     1 rx_drop     2 rx_fifo_lost  3 stream_drop
 *          4 tx_count     5 tx_ok       6 tx_timeout    7 tx_fail       8 tx_pending
 *          9 tec         10 rec        11 bus_off_count 12 last_error
 *        data[0] == 1 이면 보낸 뒤 카운터를 0 으로 만든다.
 */
static void canCmdGetStat(cmd_t *p_cmd)
{
  can_info_t info;
  uint32_t   v[13];

  canGetInfo(can_ch, &info);
  v[0]  = info.rx_count;   v[1]  = info.rx_drop;  v[2]  = info.rx_fifo_lost; v[3] = stream_drop;
  v[4]  = info.tx_count;   v[5]  = info.tx_ok;    v[6]  = info.tx_timeout;   v[7] = info.tx_fail;
  v[8]  = info.tx_pending; v[9]  = info.tec;      v[10] = info.rec;
  v[11] = info.bus_off_count;                      v[12] = info.last_error;

  cmdSendResp(p_cmd, p_cmd->packet.cmd, OK, (uint8_t *)v, sizeof(v));

  if (p_cmd->packet.length >= 1 && p_cmd->packet.data[0] == 1)
  {
    canClearCount(can_ch);
    stream_drop = 0;
  }
}

/**
 * @brief PC 연결이 끊기면 CAN 을 닫는다.
 */
void cmdCanClose(void)
{
  if (canIsOpen(can_ch))
  {
    canClose(can_ch);
    logPrintf("can : closed (PC disconnected)\r\n");
  }
}


/**
 * @brief CAN 열기 [mode, frame, baud, baud_data]
 *        열리면 저장해 둔 필터를 다시 적용한다.
 */
static void canCmdOpen(cmd_t *p_cmd)
{
  uint8_t   *p_data   = p_cmd->packet.data;
  CanMode_t  mode     = (CanMode_t)p_data[0];
  CanFrame_t frame    = (CanFrame_t)p_data[1];
  CanBaud_t  baud     = (CanBaud_t)p_data[2];
  CanBaud_t  baud_data = (CanBaud_t)p_data[3];
  bool       ret;

  ret = (p_cmd->packet.length >= 4) && canOpen(can_ch, mode, frame, baud, baud_data);
  if (ret)
  {
    canSetFilterType(filter_type);
    canConfigFilter(can_ch, 0, filter_id_type, filter_id1, filter_id2);
  }

  logPrintf("can : open mode %d frame %d baud %d/%d -> %s\r\n", mode, frame, baud, baud_data, ret ? "OK" : "NG");
  cmdSendResp(p_cmd, p_cmd->packet.cmd, ret ? OK : ERR_CAN_OPEN, NULL, 0);
}

/**
 * @brief CAN 닫기
 */
static void canCmdClose(cmd_t *p_cmd)
{
  canClose(can_ch);
  logPrintf("can : close\r\n");
  cmdSendResp(p_cmd, p_cmd->packet.cmd, OK, NULL, 0);
}

/**
 * @brief PC 에서 온 프레임을 CAN 으로 보낸다. (응답 없음)
 */
static void canCmdData(cmd_t *p_cmd)
{
  uint8_t  *p_data = p_cmd->packet.data;
  can_msg_t msg;
  uint8_t   length;

  if (p_cmd->packet.length < 10 || canIsOpen(can_ch) != true)
    return;

  canMsgInit(&msg, (CanFrame_t)p_data[2], (CanIdType_t)p_data[3], (CanDlc_t)p_data[4]);
  msg.id = (uint32_t)p_data[5] | ((uint32_t)p_data[6] << 8) | ((uint32_t)p_data[7] << 16) | ((uint32_t)p_data[8] << 24);
  length = p_data[9];

  // 패킷에 실제로 들어 있는 만큼만 복사하고, 모자라면 0 으로 채운다
  memset(msg.data, 0, sizeof(msg.data));
  memcpy(msg.data, &p_data[10], cmin((uint32_t)length, (uint32_t)(p_cmd->packet.length - 10)));
  if (msg.length > sizeof(msg.data))
    msg.length = sizeof(msg.data);

  canMsgWrite(can_ch, &msg);
}

/**
 * @brief 필터 설정 [type, id_type, id1 4, id2 4]
 *        CAN 이 닫혀 있으면 저장만 해 두고, 열 때 적용한다.
 */
static void canCmdSetFilter(cmd_t *p_cmd)
{
  uint8_t *p_data = p_cmd->packet.data;
  bool     ret    = false;

  if (p_cmd->packet.length >= 10)
  {
    filter_type    = (CanFilterType_t)p_data[0];
    filter_id_type = (CanIdType_t)p_data[1];
    memcpy(&filter_id1, &p_data[2], 4);
    memcpy(&filter_id2, &p_data[6], 4);

    canSetFilterType(filter_type);
    ret = canIsOpen(can_ch) ? canConfigFilter(can_ch, 0, filter_id_type, filter_id1, filter_id2) : true;
  }
  cmdSendResp(p_cmd, p_cmd->packet.cmd, ret ? OK : ERR_CAN_FILTER, NULL, 0);
}

/**
 * @brief 지금 필터 설정을 보낸다. [type, id_type, id1 4, id2 4]
 */
static void canCmdGetFilter(cmd_t *p_cmd)
{
  uint8_t buf[10];

  buf[0] = (uint8_t)filter_type;
  buf[1] = (uint8_t)filter_id_type;
  memcpy(&buf[2], &filter_id1, 4);
  memcpy(&buf[6], &filter_id2, 4);
  cmdSendResp(p_cmd, p_cmd->packet.cmd, OK, buf, sizeof(buf));
}
