#include "bsp.h"
#include "fault.h"


static void bspSystemClockConfig(void);
static void bspPeriphClockConfig(void);

static uint32_t chip_uid[3];


bool bspInit(void)
{
  // 부트로더가 클럭을 리셋 상태로 돌려놓고 점프했으므로, 앱도 처음부터 다시 설정한다.
  HAL_Init();
  bspSystemClockConfig();
  bspPeriphClockConfig();

  // 칩 고유번호(UID, 96비트)는 ICACHE 를 켜기 "전에" 읽어 둔다.
  // H5 는 ICACHE 가 켜진 채로 UID 영역을 읽으면 HardFault 가 날 수 있다.
  // (기존 시험 펌웨어에서 실제로 겪은 문제. 이 값으로 MAC 주소를 만든다)
  chip_uid[0] = HAL_GetUIDw0();
  chip_uid[1] = HAL_GetUIDw1();
  chip_uid[2] = HAL_GetUIDw2();

  // 앱은 ICACHE 를 켠다. Flash 에서 명령어를 읽는 속도가 크게 빨라진다.
  HAL_ICACHE_Enable();

  return true;
}

void bspGetUID(uint32_t uid[3])
{
  uid[0] = chip_uid[0];
  uid[1] = chip_uid[1];
  uid[2] = chip_uid[2];
}

void bspDeInit(void)
{
  HAL_ICACHE_Disable();
  HAL_RCC_DeInit();
  HAL_DeInit();
}

/*
 * delay()
 *   RTOS 시작 전 : HAL_Delay  -> CPU 가 그 자리에서 빙빙 돌며 기다린다
 *   RTOS 시작 후 : vTaskDelay -> 이 태스크만 잠들고, 그동안 CPU 는 다른 태스크를 돌린다
 */
void delay(uint32_t ms)
{
  if (rtosIsRunning())
    vTaskDelay(pdMS_TO_TICKS(ms));
  else
    HAL_Delay(ms);
}

uint32_t millis(void)
{
  return HAL_GetTick();
}

// 더 갈 수 없는 상황 (RTOS 스택 넘침/힙 부족, 초기화 실패 등)
//   Step 11 부터: 어디서 불렸는지 기록하고 리셋한다 (fault.c).
//   디버거로 보고 싶으면 faultReset() 에 중단점을 건다.
//   (전에는 "디버거가 붙어 있으면 __BKPT" 였는데, SWD 로 구운 뒤에는 디버거가 없어도
//    그 표시가 남아 있어서 보드가 영원히 멈춰 버릴 수 있다. 그래서 뺐다)
void Error_Handler(void)
{
  faultReset(FAULT_ERROR, (uint32_t)__builtin_return_address(0), 0, NULL);
}

// 부트로더와 같은 설정: HSE 25MHz -> PLL1 -> 250MHz (자세한 설명은 h5-boot/src/bsp/bsp.c)
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

/*
 * 주변장치 전용 클럭
 *   CAN(FDCAN) : PLL2Q 80MHz.  HSE 25MHz / 5 x 32 / 2 = 80MHz
 *                CAN 비트 속도(100K~5M)를 딱 나누어 떨어지게 만들 수 있는 값이다.
 *                (기존 시험 펌웨어가 이 보드에서 확인한 설정)
 */
static void bspPeriphClockConfig(void)
{
  RCC_PeriphCLKInitTypeDef clk = {0};

  clk.PeriphClockSelection   = RCC_PERIPHCLK_FDCAN;
  clk.PLL2.PLL2Source        = RCC_PLL2_SOURCE_HSE;
  clk.PLL2.PLL2M             = 5;
  clk.PLL2.PLL2N             = 32;
  clk.PLL2.PLL2P             = 2;
  clk.PLL2.PLL2Q             = 2;
  clk.PLL2.PLL2R             = 2;
  clk.PLL2.PLL2RGE           = RCC_PLL2_VCIRANGE_2;
  clk.PLL2.PLL2VCOSEL        = RCC_PLL2_VCORANGE_WIDE;
  clk.PLL2.PLL2ClockOut      = RCC_PLL2_DIVQ;
  clk.FdcanClockSelection    = RCC_FDCANCLKSOURCE_PLL2Q;
  if (HAL_RCCEx_PeriphCLKConfig(&clk) != HAL_OK)
  {
    Error_Handler();
  }
}
