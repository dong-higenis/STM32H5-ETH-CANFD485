/*
 * cmd.c  -  Baram 명령 프로토콜 (패킷 생성 / 파싱)
 *
 *  받기: 바이트가 하나씩 들어올 때마다 "지금 패킷의 어디를 읽는 중인지(state)"를 따라간다.
 *
 *    STX0 -> STX1 -> TYPE -> CMD_L -> CMD_H -> ERR_L -> ERR_H -> LEN_L -> LEN_H -> DATA... -> CHECKSUM
 *
 *  Baram G4 프로젝트의 cmd.c 와 같은 순서, 같은 검사 방법이다. (PC GUI 와 맞아야 하므로)
 */
#include "cmd.h"

#ifdef _USE_HW_CMD


#define CMD_STX0                0x02
#define CMD_STX1                0xFD

#define CMD_BYTE_TIMEOUT_MS     100     // 패킷 중간에 100ms 넘게 끊기면 버리고 새로 시작

enum
{
  STATE_WAIT_STX0,
  STATE_WAIT_STX1,
  STATE_WAIT_TYPE,
  STATE_WAIT_CMD_L,
  STATE_WAIT_CMD_H,
  STATE_WAIT_ERR_L,
  STATE_WAIT_ERR_H,
  STATE_WAIT_LENGTH_L,
  STATE_WAIT_LENGTH_H,
  STATE_WAIT_DATA,
  STATE_WAIT_CHECKSUM,
};


void cmdInit(cmd_t *p_cmd, cmd_driver_t *p_driver)
{
  p_cmd->is_open  = false;
  p_cmd->p_driver = p_driver;
  p_cmd->state    = STATE_WAIT_STX0;
  p_cmd->index    = 0;
  p_cmd->is_init  = true;
}

bool cmdOpen(cmd_t *p_cmd)
{
  cmd_driver_t *p_driver = p_cmd->p_driver;

  if (p_driver == NULL)
    return false;

  p_cmd->is_open  = p_driver->open(p_driver->args);
  p_cmd->state    = STATE_WAIT_STX0;
  p_cmd->pre_time = millis();
  return p_cmd->is_open;
}

bool cmdClose(cmd_t *p_cmd)
{
  cmd_driver_t *p_driver = p_cmd->p_driver;

  if (p_driver == NULL)
    return false;

  p_cmd->is_open = false;
  return p_driver->close(p_driver->args);
}

void cmdReset(cmd_t *p_cmd)
{
  p_cmd->state = STATE_WAIT_STX0;
  p_cmd->index = 0;
}

bool cmdReceivePacket(cmd_t *p_cmd)
{
  cmd_driver_t *p_driver = p_cmd->p_driver;
  cmd_packet_t *p_packet = &p_cmd->packet;
  bool          ret      = false;
  uint8_t       rx_data;

  if (p_cmd->is_open != true)
    return false;

  while (ret != true && p_driver->available(p_driver->args) > 0)
  {
    rx_data = p_driver->read(p_driver->args);

    if (millis() - p_cmd->pre_time >= CMD_BYTE_TIMEOUT_MS)
    {
      p_cmd->state = STATE_WAIT_STX0;
    }
    p_cmd->pre_time = millis();

    switch (p_cmd->state)
    {
      case STATE_WAIT_STX0:
        if (rx_data == CMD_STX0)
        {
          p_packet->check_sum = rx_data;
          p_packet->err_code  = 0;
          p_cmd->state = STATE_WAIT_STX1;
        }
        break;

      case STATE_WAIT_STX1:
        if (rx_data == CMD_STX1)
        {
          p_packet->check_sum += rx_data;
          p_cmd->state = STATE_WAIT_TYPE;
        }
        else
        {
          p_cmd->state = STATE_WAIT_STX0;
        }
        break;

      case STATE_WAIT_TYPE:
        p_packet->type = (cmd_type_t)rx_data;
        p_packet->check_sum += rx_data;
        p_cmd->state = STATE_WAIT_CMD_L;
        break;

      case STATE_WAIT_CMD_L:
        p_packet->cmd = rx_data;
        p_packet->check_sum += rx_data;
        p_cmd->state = STATE_WAIT_CMD_H;
        break;

      case STATE_WAIT_CMD_H:
        p_packet->cmd |= (rx_data << 8);
        p_packet->check_sum += rx_data;
        p_cmd->state = STATE_WAIT_ERR_L;
        break;

      case STATE_WAIT_ERR_L:
        p_packet->err_code = rx_data;
        p_packet->check_sum += rx_data;
        p_cmd->state = STATE_WAIT_ERR_H;
        break;

      case STATE_WAIT_ERR_H:
        p_packet->err_code |= (rx_data << 8);
        p_packet->check_sum += rx_data;
        p_cmd->state = STATE_WAIT_LENGTH_L;
        break;

      case STATE_WAIT_LENGTH_L:
        p_packet->length = rx_data;
        p_packet->check_sum += rx_data;
        p_cmd->state = STATE_WAIT_LENGTH_H;
        break;

      case STATE_WAIT_LENGTH_H:
        p_packet->length |= (rx_data << 8);
        p_packet->check_sum += rx_data;

        if (p_packet->length == 0)
        {
          p_cmd->state = STATE_WAIT_CHECKSUM;
        }
        else if (p_packet->length <= CMD_MAX_DATA_LENGTH)
        {
          p_cmd->index = 0;
          p_cmd->state = STATE_WAIT_DATA;
        }
        else
        {
          // 너무 긴 패킷: 에러로 알리고 버린다
          p_packet->err_code = ERR_CMD_MAX_LENGTH;
          p_cmd->state = STATE_WAIT_STX0;
          ret = true;
        }
        break;

      case STATE_WAIT_DATA:
        p_packet->data[p_cmd->index++] = rx_data;
        p_packet->check_sum += rx_data;
        if (p_cmd->index == p_packet->length)
        {
          p_cmd->state = STATE_WAIT_CHECKSUM;
        }
        break;

      case STATE_WAIT_CHECKSUM:
        // 체크섬 검사: 앞의 모든 바이트를 더한 값의 "2의 보수"가 마지막 바이트와 같아야 한다
        p_packet->check_sum_recv = rx_data;
        p_packet->check_sum      = (~p_packet->check_sum) + 1;
        if (p_packet->check_sum != p_packet->check_sum_recv)
        {
          p_packet->err_code = ERR_CMD_CHECKSUM;
        }
        p_cmd->state = STATE_WAIT_STX0;
        ret = true;
        break;
    }
  }

  return ret;
}

// 패킷 1개를 buf 에 만든다. 만든 길이를 돌려준다 (실패하면 0)
uint32_t cmdMakePacket(uint8_t *buf, uint32_t buf_size, cmd_type_t type, uint16_t cmd, uint16_t err_code,
                       const uint8_t *p_data, uint32_t length)
{
  uint32_t index     = 0;
  uint8_t  check_sum = 0;

  if (buf == NULL || length > CMD_MAX_DATA_LENGTH || buf_size < length + 10)
    return 0;

  buf[index++] = CMD_STX0;
  buf[index++] = CMD_STX1;
  buf[index++] = (uint8_t)type;
  buf[index++] = (cmd >> 0) & 0xFF;
  buf[index++] = (cmd >> 8) & 0xFF;
  buf[index++] = (err_code >> 0) & 0xFF;
  buf[index++] = (err_code >> 8) & 0xFF;
  buf[index++] = (length >> 0) & 0xFF;
  buf[index++] = (length >> 8) & 0xFF;

  if (length > 0 && p_data != NULL)
  {
    memcpy(&buf[index], p_data, length);
    index += length;
  }

  for (uint32_t i = 0; i < index; i++)
  {
    check_sum += buf[i];
  }
  buf[index++] = (~check_sum) + 1;

  return index;
}

// 이미 다 받은 패킷 1개(buf)를 파싱한다. UDP 처럼 패킷 단위로 오는 경로에서 쓴다.
bool cmdParsePacket(const uint8_t *buf, uint32_t buf_len, cmd_packet_t *p_packet)
{
  uint16_t length;
  uint8_t  check_sum = 0;

  if (buf == NULL || buf_len < 10 || buf[0] != CMD_STX0 || buf[1] != CMD_STX1)
    return false;

  length = buf[7] | (buf[8] << 8);
  if (length > CMD_MAX_DATA_LENGTH || buf_len < (uint32_t)length + 10)
    return false;

  for (uint32_t i = 0; i < (uint32_t)length + 10; i++)
  {
    check_sum += buf[i];                  // 검사합까지 다 더하면 0 이 되어야 한다
  }
  if (check_sum != 0)
    return false;

  p_packet->type     = (cmd_type_t)buf[2];
  p_packet->cmd      = buf[3] | (buf[4] << 8);
  p_packet->err_code = buf[5] | (buf[6] << 8);
  p_packet->length   = length;
  memcpy(p_packet->data, &buf[9], length);
  return true;
}

bool cmdSend(cmd_t *p_cmd, cmd_type_t type, uint16_t cmd, uint16_t err_code, const uint8_t *p_data, uint32_t length)
{
  cmd_driver_t *p_driver = p_cmd->p_driver;
  uint32_t      size;

  if (p_cmd->is_open != true)
    return false;

  size = cmdMakePacket(p_cmd->tx_buf, sizeof(p_cmd->tx_buf), type, cmd, err_code, p_data, length);
  if (size == 0)
    return false;

  return (p_driver->write(p_driver->args, p_cmd->tx_buf, size) == size);
}

bool cmdSendResp(cmd_t *p_cmd, uint16_t cmd, uint16_t err_code, const uint8_t *p_data, uint32_t length)
{
  return cmdSend(p_cmd, PKT_TYPE_RESP, cmd, err_code, p_data, length);
}

#endif
