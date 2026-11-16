/*
 * hw_def.h
 *
 *  이 프로젝트(부트로더)가 "어떤 하드웨어를 쓰는지" 고르는 설정 파일.
 *  _USE_HW_xxx 를 지우면 해당 드라이버는 컴파일에서 빠진다.
 */
#ifndef HW_DEF_H_
#define HW_DEF_H_

#include "bsp.h"


#define _DEF_FIRMWARE_VERSION     "B261116R3"           // B=boot, 26-11-16, R3 (Step 11 고장 기록 보여주기)
#define _DEF_BOARD_NAME           "H5-ETH-FDCAN-BOOT"


#define _USE_HW_LED
#define      HW_LED_MAX_CH          1
#define      HW_LED_CH_STATUS       _DEF_LED1           // PC13, LOW 점등

#define _USE_HW_UART
#define      HW_UART_MAX_CH         1
#define      HW_UART_CH_DEBUG       _DEF_UART1          // USART1, PB15 TX / PB14 RX (SWAP)

#define _USE_HW_LOG
#define      HW_LOG_CH              HW_UART_CH_DEBUG
#define      HW_LOG_BAUD            115200

#define _USE_HW_RESET

#define _USE_HW_FLASH                                   // 업데이트 영역 -> 앱 영역 복사


#endif
