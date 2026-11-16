/*
 * hw_def.h  (애플리케이션)
 */
#ifndef HW_DEF_H_
#define HW_DEF_H_

#include "bsp.h"


#define _DEF_FIRMWARE_VERSION     "V261116R13"          // V=app, 26-11-16, R13 (CAN 손실 카운터, 자동 재전송)
#define _DEF_BOARD_NAME           "H5-ETH-FDCAN-FW"     // GUI 가 다운로드 전에 이 이름을 검사한다


#define _USE_HW_LED
#define      HW_LED_MAX_CH          1
#define      HW_LED_CH_STATUS       _DEF_LED1

#define _USE_HW_UART
#define      HW_UART_MAX_CH         2
#define      HW_UART_CH_DEBUG       _DEF_UART1          // USART1  : J1 디버그 (로그, CLI)
#define      HW_UART_CH_RS485       _DEF_UART2          // USART11 : RS485

#define _USE_HW_LOG
#define      HW_LOG_CH              HW_UART_CH_DEBUG
#define      HW_LOG_BAUD            115200

#define _USE_HW_CLI
#define      HW_CLI_CH              HW_UART_CH_DEBUG
#define      HW_CLI_CMD_LIST_MAX    32
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_BUF_MAX    64

#define _USE_HW_RESET

#define _USE_HW_FAULT                                   // 죽으면 원인 기록하고 리셋 (Step 11)
#define _USE_HW_WDG                                     // 하드웨어 워치독 IWDG (Step 11)
#define      HW_WDG_TIMEOUT_MS      4000                // 4초 안에 갱신하지 않으면 리셋

#define _USE_HW_WIZ_QSPI                                // W6300 과 이어진 QSPI
#define _USE_HW_WIZNET                                  // 네트워크 관리 (DHCP 등)
#define      HW_WIZNET_SOCKET_DHCP  0                   // W6300 소켓 0번: DHCP

#define _USE_HW_TCP_SERVER
#define      HW_NET_SOCKET_CMD      1                   // W6300 소켓 1번: PC 접속용 TCP 서버
#define      HW_NET_CMD_PORT        NET_TCP_CMD_PORT    // 5000 (common/net_proto.h)

#define _USE_HW_NET_FIND
#define      HW_NET_SOCKET_FIND     2                   // W6300 소켓 2번: 보드 찾기 (UDP)
#define      HW_NET_FIND_PORT       NET_UDP_FIND_PORT   // 5001

#define _USE_HW_CMD                                     // Baram 명령 프로토콜
#define      HW_CMD_MAX_DATA_LENGTH 1024

#define _USE_HW_FLASH                                   // 업데이트 영역(Bank2)에 새 펌웨어 쓰기

#define _USE_HW_CAN                                     // CAN-FD (FDCAN1)
#define      HW_CAN_MAX_CH          1
#define      HW_CAN_MSG_RX_BUF_MAX  128                 // RX 큐 크기 (프레임 수)
#define      HW_CAN_MSG_TX_BUF_MAX  64                  // TX 큐 크기 (칩 FIFO 는 3개뿐)
#define      _DEF_CAN1              0


#endif
