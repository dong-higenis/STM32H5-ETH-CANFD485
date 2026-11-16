/*
 * stm32h5xx_it.c  (애플리케이션)
 *
 *  SVC_Handler / PendSV_Handler / SysTick_Handler 는 여기 없다.
 *  -> FreeRTOS 포트(lib/FreeRTOS/portable/GCC/ARM_CM33_NTZ)가 직접 정의한다.
 *
 *  NMI / HardFault / MemManage / BusFault / UsageFault 도 여기 없다.
 *  -> Step 11 부터 hw/driver/fault.c 가 원인을 기록하고 리셋한다.
 */
#include "bsp.h"


void SecureFault_Handler(void)
{
  while (1)
  {
  }
}

void DebugMon_Handler(void)
{
}
