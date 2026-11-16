/*
 * bsp.h  (Board Support Package)
 *
 *  MCU 자체를 초기화하는 계층: HAL 초기화, 클럭 트리, RTOS.
 *  보드 위의 부품(LED, UART...)은 여기서 다루지 않는다 -> hw 계층.
 */
#ifndef BSP_H_
#define BSP_H_

#include "def.h"
#include "stm32h5xx_hal.h"
#include "rtos.h"


bool     bspInit(void);
void     bspDeInit(void);

void     delay(uint32_t ms);
uint32_t millis(void);
void     bspGetUID(uint32_t uid[3]);    // 칩 고유번호 (부팅 때 읽어 둔 값)

void     Error_Handler(void);


#endif
