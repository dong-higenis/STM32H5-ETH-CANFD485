/*
 * can.h  -  CAN-FD 드라이버
 *
 *  아래 enum 의 숫자는 Baram G4 펌웨어 / PC GUI(tab_can.py, cmd_can.py)와 같아야 한다.
 *  GUI 는 콤보박스의 "몇 번째 항목"을 그대로 숫자로 보낸다.
 */
#ifndef COMMON_HW_CAN_H_
#define COMMON_HW_CAN_H_

#include "hw_def.h"

#ifdef _USE_HW_CAN

#define CAN_MAX_CH            HW_CAN_MAX_CH
#define CAN_MSG_RX_BUF_MAX    HW_CAN_MSG_RX_BUF_MAX
#define CAN_MSG_TX_BUF_MAX    HW_CAN_MSG_TX_BUF_MAX


typedef enum { CAN_100K, CAN_125K, CAN_250K, CAN_500K, CAN_1M, CAN_2M, CAN_4M, CAN_5M } CanBaud_t;
typedef enum { CAN_NORMAL, CAN_MONITOR, CAN_LOOPBACK } CanMode_t;
typedef enum { CAN_CLASSIC, CAN_FD_NO_BRS, CAN_FD_BRS } CanFrame_t;
typedef enum { CAN_STD, CAN_EXT } CanIdType_t;
typedef enum
{
  CAN_DLC_0, CAN_DLC_1, CAN_DLC_2, CAN_DLC_3, CAN_DLC_4, CAN_DLC_5, CAN_DLC_6, CAN_DLC_7, CAN_DLC_8,
  CAN_DLC_12, CAN_DLC_16, CAN_DLC_20, CAN_DLC_24, CAN_DLC_32, CAN_DLC_48, CAN_DLC_64
} CanDlc_t;
typedef enum { CAN_ID_MASK, CAN_ID_RANGE } CanFilterType_t;


typedef struct
{
  uint32_t    id;
  uint16_t    length;         // 실제 데이터 바이트 수 (0~64)
  uint8_t     data[64];
  CanDlc_t    dlc;
  CanIdType_t id_type;
  CanFrame_t  frame;
  uint32_t    timestamp;      // 받은 시각 (ms)
} can_msg_t;

// 카운터들 (canClearCount() 로 0 부터 다시 셀 수 있다)
//   보내기:  tx_count(TX 큐에 들어감) = tx_ok(선에 나감) + tx_timeout(포기) + tx_pending(아직 가는 중)
//            tx_fail = TX 큐에도 못 넣음 (큐 꽉 참, 닫힘, 모니터 모드, 잘못된 길이)
//   받기:    rx_count(칩에서 꺼냄) = RX 큐에 들어감 + rx_drop(RX 큐 꽉 참)
//            rx_fifo_lost = 칩 RX FIFO(3개)가 넘친 "횟수" (한 번에 몇 개 잃었는지는 칩도 모른다 -> 최소값)
typedef struct
{
  bool      is_open;
  CanMode_t mode;
  uint32_t  rx_count;
  uint32_t  tx_count;
  uint32_t  tx_fail;         // TX 큐에도 못 넣음
  uint32_t  rx_drop;         // 받기 큐가 꽉 차서 버린 수
  uint32_t  tec;             // 송신 에러 카운터 (버스 상태)
  uint32_t  rec;             // 수신 에러 카운터
  bool      bus_off;
  uint32_t  last_error;      // 마지막 프로토콜 에러 코드 (LEC)

  uint32_t  tx_ok;           // 실제로 선에 나감 (상대가 ACK 함)
  uint32_t  tx_timeout;      // 오래 못 보내서 포기 (받아 주는 장치가 없음 등)
  uint32_t  tx_pending;      // 지금 TX 큐 + 칩 TX FIFO 에 있는 수
  uint32_t  rx_fifo_lost;    // 칩 RX FIFO 넘침 횟수
  uint32_t  bus_off_count;   // 버스오프가 되어 다시 켠 횟수
} can_info_t;


bool     canInit(void);
bool     canOpen(uint8_t ch, CanMode_t mode, CanFrame_t frame, CanBaud_t baud, CanBaud_t baud_data);
void     canClose(uint8_t ch);
bool     canIsOpen(uint8_t ch);
bool     canMsgInit(can_msg_t *p_msg, CanFrame_t frame, CanIdType_t id_type, CanDlc_t dlc);
bool     canMsgWrite(uint8_t ch, can_msg_t *p_msg);         // 송신 FIFO 에 넣기만 한다 (기다리지 않음)
uint32_t canMsgAvailable(uint8_t ch);
uint32_t canMsgTxFree(uint8_t ch);                          // TX 큐 빈 슬롯 수 (0 이면 더 못 받음)
bool     canMsgRead(uint8_t ch, can_msg_t *p_msg);
bool     canSetFilterType(CanFilterType_t type);
bool     canConfigFilter(uint8_t ch, uint8_t index, CanIdType_t id_type, uint32_t id1, uint32_t id2);
bool     canGetInfo(uint8_t ch, can_info_t *p_info);
void     canClearCount(uint8_t ch);
void     canUpdate(uint8_t ch);                              // 1ms 마다: 오래 못 보낸 프레임 포기
uint8_t  canDlcToLength(CanDlc_t dlc);

#endif

#endif
