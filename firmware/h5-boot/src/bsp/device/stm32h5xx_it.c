#include "bsp.h"


volatile uint32_t bsp_flash_ecc_error = 0;

void NMI_Handler(void)
{
  if (FLASH->ECCDETR & FLASH_ECCR_ECCD)
  {
    FLASH->ECCDETR = FLASH_ECCR_ECCD;   // 1 을 써서 지운다
    bsp_flash_ecc_error++;
    return;
  }

  // 그 밖의 NMI(클럭 고장 등)는 원인을 알 수 없으므로 멈춘다
  while (1)
  {
  }
}

void HardFault_Handler(void)
{
  while (1)
  {
  }
}

void MemManage_Handler(void)
{
  while (1)
  {
  }
}

void BusFault_Handler(void)
{
  while (1)
  {
  }
}

void UsageFault_Handler(void)
{
  while (1)
  {
  }
}

void SecureFault_Handler(void)
{
  while (1)
  {
  }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();   
}
