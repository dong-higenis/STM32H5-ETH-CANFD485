/*
 * cmd.h  -  Baram 명령 프로토콜
 *
 *  PC 와 주고받는 패킷 구조:
 *
 *    02 FD | type | cmd(2) | err(2) | length(2) | data ... | checksum
 *    STX      종류   명령번호  에러      data 길이    내용        체크섬
 *
 *  패킷을 어떤 통신 경로(UART, USB, TCP)로 보낼지는 cmd_driver_t 가 정한다.
 *  그래서 같은 cmd 코드를 G4 보드(USB)와 이 보드(TCP)가 함께 쓸 수 있다.
 */
#ifndef COMMON_HW_CMD_H_
#define COMMON_HW_CMD_H_

#include "hw_def.h"

#ifdef _USE_HW_CMD

#define CMD_MAX_DATA_LENGTH   HW_CMD_MAX_DATA_LENGTH
#define CMD_OK                0x0000


typedef enum
{
  PKT_TYPE_CMD   = 0x00,      // PC -> 보드 : 명령
  PKT_TYPE_RESP  = 0x01,      // 보드 -> PC : 명령에 대한 응답
  PKT_TYPE_EVENT = 0x02,
  PKT_TYPE_LOG   = 0x03,
  PKT_TYPE_PING  = 0x04,
  PKT_TYPE_CAN   = 0x05,      // CAN 프레임 (양방향, 응답 없음)
  PKT_TYPE_UART  = 0x06,      // RS485 데이터 (양방향, 응답 없음)
} cmd_type_t;

typedef struct
{
  cmd_type_t type;
  uint16_t   cmd;
  uint16_t   err_code;
  uint16_t   length;
  uint8_t    check_sum;
  uint8_t    check_sum_recv;
  uint8_t    data[CMD_MAX_DATA_LENGTH];
} cmd_packet_t;

// 패킷을 주고받는 통신 드라이버. TCP, UART 등 종류마다 이 6개 함수를 만들어 준다.
typedef struct
{
  void     *args;
  bool     (*open)(void *args);
  bool     (*close)(void *args);
  uint32_t (*available)(void *args);
  bool     (*flush)(void *args);
  uint8_t  (*read)(void *args);
  uint32_t (*write)(void *args, const uint8_t *p_data, uint32_t length);
} cmd_driver_t;

typedef struct
{
  bool          is_init;
  bool          is_open;
  uint8_t       state;
  uint32_t      pre_time;
  uint32_t      index;
  cmd_driver_t *p_driver;
  cmd_packet_t  packet;                               // 받은 패킷
  uint8_t       tx_buf[CMD_MAX_DATA_LENGTH + 10];     // 보낼 패킷
} cmd_t;


void cmdInit(cmd_t *p_cmd, cmd_driver_t *p_driver);
bool cmdOpen(cmd_t *p_cmd);
bool cmdClose(cmd_t *p_cmd);
void cmdReset(cmd_t *p_cmd);                          // 받던 패킷을 버리고 처음부터
bool cmdReceivePacket(cmd_t *p_cmd);                  // 패킷 하나가 완성되면 true
bool cmdSend(cmd_t *p_cmd, cmd_type_t type, uint16_t cmd, uint16_t err_code, const uint8_t *p_data, uint32_t length);
bool cmdSendResp(cmd_t *p_cmd, uint16_t cmd, uint16_t err_code, const uint8_t *p_data, uint32_t length);

// 드라이버 없이 메모리에서 패킷 1개를 만들고 / 파싱한다 (UDP 처럼 패킷 단위로 오가는 경우)
uint32_t cmdMakePacket(uint8_t *buf, uint32_t buf_size, cmd_type_t type, uint16_t cmd, uint16_t err_code,
                       const uint8_t *p_data, uint32_t length);
bool     cmdParsePacket(const uint8_t *buf, uint32_t buf_len, cmd_packet_t *p_packet);

#endif

#endif
