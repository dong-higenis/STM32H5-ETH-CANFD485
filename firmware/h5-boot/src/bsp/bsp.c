#include "bsp.h"


static void bspSystemClockConfig(void);


bool bspInit(void)
{
  HAL_Init();

  bspSystemClockConfig();

  // 부트로더는 ICACHE 를 켜지 않는다.
  return true;
}

 /**
  * @brief 앱단으로 Jump 하기 전에 bootloader에서 켜둔 것들을 모두 되돌림으로써
  *   앱이 리셋된 칩상에 돌아갈수있도록 한다.
  * 
  *   단 HAL_RCC_DeInit() 함수 내부에서는 시간을 재는 기능이 있으므로,
  *   SysTick을 비활성화 하기 전에 호출한다.
  */
void bspDeInit(void)
{
  // 1) 클럭을 리셋 상태(HSI 64MHz)로, 주변장치를 리셋 상태로
  HAL_RCC_DeInit();
  HAL_DeInit();

  // 2) 이제 인터럽트를 막고
  __disable_irq();

  // 3) SysTick 정지
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;

  // 4) 모든 인터럽트 소스를 끄고, 이미 대기 중인 것도 지운다
  for (uint32_t i = 0; i < ARRAY_COUNT(NVIC->ICER); i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFF;
    NVIC->ICPR[i] = 0xFFFFFFFF;
  }

  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;   // 대기 중인 SysTick 예외도 지운다
}

void delay(uint32_t ms)
{
  HAL_Delay(ms);
}

uint32_t millis(void)
{
  return HAL_GetTick();
}

void Error_Handler(void)
{
  // 디버거가 연결돼 있으면 여기서 멈춰서 원인을 볼 수 있게 한다
  if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)
  {
    __BKPT(0);
  }

  __disable_irq();
  while (1)
  {
  }
}

static void bspSystemClockConfig(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
  {
  }

  osc.OscillatorType  = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState        = RCC_HSE_ON;
  osc.PLL.PLLState    = RCC_PLL_ON;
  osc.PLL.PLLSource   = RCC_PLL1_SOURCE_HSE;
  osc.PLL.PLLM        = 2;
  osc.PLL.PLLN        = 40;
  osc.PLL.PLLP        = 2;
  osc.PLL.PLLQ        = 2;
  osc.PLL.PLLR        = 2;
  osc.PLL.PLLRGE      = RCC_PLL1_VCIRANGE_3;
  osc.PLL.PLLVCOSEL   = RCC_PLL1_VCORANGE_WIDE;
  osc.PLL.PLLFRACN    = 0;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    Error_Handler();
  }

  clk.ClockType       = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                        RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK3;
  clk.SYSCLKSource    = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider   = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider  = RCC_HCLK_DIV1;
  clk.APB2CLKDivider  = RCC_HCLK_DIV1;
  clk.APB3CLKDivider  = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }

  __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2);
}
