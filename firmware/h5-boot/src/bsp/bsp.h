#ifndef BSP_H_
#define BSP_H_

#include "def.h"
#include "stm32h5xx_hal.h"


bool     bspInit(void);
void     bspDeInit(void);

void     delay(uint32_t ms);
uint32_t millis(void);

void     Error_Handler(void);


#endif
