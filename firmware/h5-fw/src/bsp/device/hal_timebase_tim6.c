/*
 * hal_timebase_tim6.c
 *
 *  HAL 의 1ms 시계(HAL_GetTick, HAL_Delay)를 SysTick 대신 TIM6 로 돌린다.
 *
 *  왜?
 *    SysTick 은 FreeRTOS 가 가져간다. (port.c 에 SysTick_Handler 가 들어 있다)
 *    HAL 도 시간 재기가 필요하므로, 남는 타이머(TIM6) 하나를 HAL 전용 시계로 준다.
 *
 *  HAL 의 HAL_InitTick() 은 __weak 이라서, 같은 이름의 함수를 만들면 이쪽이 쓰인다.
 *  HAL_Init() 과 HAL_RCC_ClockConfig() 가 이 함수를 부른다(클럭이 바뀌면 다시 맞춘다).
 */
#include "bsp.h"


HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
  uint32_t pclk1;
  uint32_t tim_clk;

  if (TickPriority >= (1UL << __NVIC_PRIO_BITS))
    return HAL_ERROR;

  __HAL_RCC_TIM6_CLK_ENABLE();

  // TIM6 는 APB1 에 붙어 있다. APB1 을 나누지 않으면 타이머 클럭 = PCLK1, 나누면 x2
  pclk1 = HAL_RCC_GetPCLK1Freq();
  if (((RCC->CFGR2 & RCC_CFGR2_PPRE1) >> RCC_CFGR2_PPRE1_Pos) < 4)
    tim_clk = pclk1;
  else
    tim_clk = pclk1 * 2;

  // 1MHz 로 세고(PSC), 1000 번마다(ARR) 인터럽트 -> 1ms
  TIM6->CR1  = 0;
  TIM6->PSC  = (tim_clk / 1000000UL) - 1;
  TIM6->ARR  = 1000 - 1;
  TIM6->EGR  = TIM_EGR_UG;          // PSC/ARR 즉시 적용
  TIM6->SR   = 0;                   // UG 로 생긴 플래그 지우기
  TIM6->DIER = TIM_DIER_UIE;        // 넘칠 때 인터럽트
  TIM6->CR1  = TIM_CR1_CEN;         // 시작

  HAL_NVIC_SetPriority(TIM6_IRQn, TickPriority, 0);
  HAL_NVIC_EnableIRQ(TIM6_IRQn);

  uwTickPrio = TickPriority;
  return HAL_OK;
}

void HAL_SuspendTick(void)
{
  TIM6->DIER &= ~TIM_DIER_UIE;
}

void HAL_ResumeTick(void)
{
  TIM6->DIER |= TIM_DIER_UIE;
}

void TIM6_IRQHandler(void)
{
  if (TIM6->SR & TIM_SR_UIF)
  {
    TIM6->SR = ~TIM_SR_UIF;
    HAL_IncTick();
  }
}
