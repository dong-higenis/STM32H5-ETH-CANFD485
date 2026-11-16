#ifndef COMMON_HW_UART_H_
#define COMMON_HW_UART_H_

#include "hw_def.h"

#ifdef _USE_HW_UART

#define UART_MAX_CH   HW_UART_MAX_CH

typedef struct
{
  uint32_t rx_count;
  uint32_t tx_count;
  uint32_t rx_overrun;      // 너무 빨리 들어와서 놓친 수
  uint32_t rx_error;        // 프레이밍/노이즈/패리티 에러
  uint32_t tx_drop;         // 송신 버퍼가 꽉 차서 못 넣은 바이트
} uart_stat_t;

bool     uartInit(void);
bool     uartOpen(uint8_t ch, uint32_t baud);
bool     uartClose(uint8_t ch);
bool     uartIsOpen(uint8_t ch);
uint32_t uartAvailable(uint8_t ch);
bool     uartFlush(uint8_t ch);
uint8_t  uartRead(uint8_t ch);
uint32_t uartWrite(uint8_t ch, const uint8_t *p_data, uint32_t length);
uint32_t uartPrintf(uint8_t ch, const char *fmt, ...);
uint32_t uartGetBaud(uint8_t ch);
bool     uartGetStat(uint8_t ch, uart_stat_t *p_stat);

#endif

#endif
