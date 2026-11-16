/*
 * wiz_qspi.c  -  W6300 QSPI 전송
 *
 *  핀 (회로도 / 기존 시험 펌웨어에서 실제 보드로 확인됨)
 *    PA3  CLK  (AF3)      PB10 NCS (AF9)
 *    PC3  IO0  (AF9)      PB0  IO1 (AF6)
 *    PC2  IO2  (AF9)      PA1  IO3 (AF9)
 *    PC0  RST  (출력, LOW = 리셋)
 *    PC4  INT  (입력, LOW = 인터럽트) - 아직은 상태만 읽는다
 *
 *  W6300 QSPI 한 번의 통신 모양 (WIZnet W6300 데이터시트)
 *
 *    [명령 1바이트] [주소 2바이트] [dummy 1바이트] [데이터 ...]
 *      1줄로 보냄    4줄로 보냄     4줄(2클럭)     4줄
 *
 *    명령 = 1 0 W B B B B B
 *           │ │ │ └──┴─ block (5비트)
 *           │ │ └ 0=읽기 1=쓰기
 *           └─┴ 10 = Quad 모드
 *
 *    dummy: 읽을 때는 W6300 이 데이터를 준비하는 시간(dummy 2클럭),
 *          쓸 때는 0x00 한 바이트를 보낸다(WIZnet 예제와 같은 방식).
 *
 *  속도: 250MHz / 25 = 10MHz. 기존 시험 펌웨어가 이 보드에서 확인한 값이다.
 *        (Baram 의 다른 보드는 62.5MHz. 나중에 올려 볼 수 있다)
 */
#include "wiz_qspi.h"

#ifdef _USE_HW_WIZ_QSPI


#define WIZ_QSPI_PRESCALER      24        // 250MHz / (24+1) = 10MHz
#define WIZ_QSPI_TIMEOUT_MS     100

#define WIZ_INST_QUAD           0x80
#define WIZ_INST_WRITE          0x20


static XSPI_HandleTypeDef hxspi;
static bool               is_init     = false;
static uint32_t           error_count = 0;


static void wizQspiInitPins(void);
static bool wizQspiTransfer(bool is_write, uint8_t block, uint16_t addr, uint8_t *p_data, uint32_t length);


bool wizQspiInit(void)
{
  wizQspiInitPins();

  __HAL_RCC_OSPI_CONFIG(RCC_OSPICLKSOURCE_HCLK);   // QSPI 클럭 = HCLK 250MHz
  __HAL_RCC_OSPI1_CLK_ENABLE();

  hxspi.Instance                     = OCTOSPI1;
  hxspi.Init.FifoThresholdByte       = 4;
  hxspi.Init.MemoryMode              = HAL_XSPI_SINGLE_MEM;
  hxspi.Init.MemoryType              = HAL_XSPI_MEMTYPE_MICRON;
  hxspi.Init.MemorySize              = HAL_XSPI_SIZE_1MB;     // 메모리가 아니라서 의미 없음 (주소 16비트만 씀)
  hxspi.Init.ChipSelectHighTimeCycle = 4;                     // 통신 사이 CS 를 HIGH 로 두는 최소 시간
  hxspi.Init.FreeRunningClock        = HAL_XSPI_FREERUNCLK_DISABLE;
  hxspi.Init.ClockMode               = HAL_XSPI_CLOCK_MODE_0;
  hxspi.Init.WrapSize                = HAL_XSPI_WRAP_NOT_SUPPORTED;
  hxspi.Init.ClockPrescaler          = WIZ_QSPI_PRESCALER;
  hxspi.Init.SampleShifting          = HAL_XSPI_SAMPLE_SHIFT_NONE;
  hxspi.Init.DelayHoldQuarterCycle   = HAL_XSPI_DHQC_DISABLE;
  hxspi.Init.ChipSelectBoundary      = HAL_XSPI_BONDARYOF_NONE;
  hxspi.Init.DelayBlockBypass        = HAL_XSPI_DELAY_BLOCK_BYPASS;
  hxspi.Init.Refresh                 = 0;

  is_init = (HAL_XSPI_Init(&hxspi) == HAL_OK);
  return is_init;
}

void wizQspiReset(void)
{
  // 기존 시험 펌웨어가 이 보드에서 쓴 시간 그대로 (100ms 누르고, 500ms 기다림)
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_RESET);
  delay(100);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_SET);
  delay(500);
}

bool wizQspiRead(uint8_t block, uint16_t addr, uint8_t *p_data, uint32_t length)
{
  return wizQspiTransfer(false, block, addr, p_data, length);
}

bool wizQspiWrite(uint8_t block, uint16_t addr, const uint8_t *p_data, uint32_t length)
{
  return wizQspiTransfer(true, block, addr, (uint8_t *)p_data, length);
}

uint32_t wizQspiGetErrorCount(void)
{
  return error_count;
}

bool wizQspiIntIsActive(void)
{
  return HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_4) == GPIO_PIN_RESET;
}


static bool wizQspiTransfer(bool is_write, uint8_t block, uint16_t addr, uint8_t *p_data, uint32_t length)
{
  XSPI_RegularCmdTypeDef cmd = {0};
  HAL_StatusTypeDef      status;

  if (!is_init || p_data == NULL || length == 0 || block > 31)
    return false;

  cmd.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
  cmd.IOSelect           = HAL_XSPI_SELECT_IO_3_0;

  // [명령 1바이트] - 1줄
  cmd.Instruction        = WIZ_INST_QUAD | (is_write ? WIZ_INST_WRITE : 0) | block;
  cmd.InstructionMode    = HAL_XSPI_INSTRUCTION_1_LINE;
  cmd.InstructionWidth   = HAL_XSPI_INSTRUCTION_8_BITS;
  cmd.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;

  // [주소 2바이트] - 4줄
  cmd.Address            = addr;
  cmd.AddressMode        = HAL_XSPI_ADDRESS_4_LINES;
  cmd.AddressWidth       = HAL_XSPI_ADDRESS_16_BITS;
  cmd.AddressDTRMode     = HAL_XSPI_ADDRESS_DTR_DISABLE;

  // [dummy] - 쓰기: 0x00 한 바이트 / 읽기: 2클럭 기다림
  if (is_write)
  {
    cmd.AlternateBytesMode  = HAL_XSPI_ALT_BYTES_4_LINES;
    cmd.AlternateBytesWidth = HAL_XSPI_ALT_BYTES_8_BITS;
    cmd.AlternateBytes      = 0x00;
    cmd.DummyCycles         = 0;
  }
  else
  {
    cmd.AlternateBytesMode  = HAL_XSPI_ALT_BYTES_NONE;
    cmd.DummyCycles         = 2;
  }

  // [데이터] - 4줄
  cmd.DataMode           = HAL_XSPI_DATA_4_LINES;
  cmd.DataLength         = length;
  cmd.DataDTRMode        = HAL_XSPI_DATA_DTR_DISABLE;
  cmd.DQSMode            = HAL_XSPI_DQS_DISABLE;

  status = HAL_XSPI_Command(&hxspi, &cmd, WIZ_QSPI_TIMEOUT_MS);
  if (status == HAL_OK)
  {
    if (is_write)
      status = HAL_XSPI_Transmit(&hxspi, p_data, WIZ_QSPI_TIMEOUT_MS);
    else
      status = HAL_XSPI_Receive(&hxspi, p_data, WIZ_QSPI_TIMEOUT_MS);
  }

  if (status != HAL_OK)
  {
    error_count++;
    HAL_XSPI_Abort(&hxspi);       // 막힌 통신을 끊고 다음 통신이 가능하게 한다
    if (!is_write)
      memset(p_data, 0, length);
    return false;
  }
  return true;
}

static void wizQspiInitPins(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  // RST: 처음엔 LOW(리셋 상태)로 시작
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_RESET);
  gpio.Pin   = GPIO_PIN_0;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &gpio);

  // INT: 입력
  gpio.Pin  = GPIO_PIN_4;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &gpio);

  // QSPI 핀
  gpio.Mode  = GPIO_MODE_AF_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;

  gpio.Pin       = GPIO_PIN_3;                    // PA3 CLK
  gpio.Alternate = GPIO_AF3_OCTOSPI1;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin       = GPIO_PIN_1;                    // PA1 IO3
  gpio.Alternate = GPIO_AF9_OCTOSPI1;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin       = GPIO_PIN_0;                    // PB0 IO1
  gpio.Alternate = GPIO_AF6_OCTOSPI1;
  HAL_GPIO_Init(GPIOB, &gpio);

  gpio.Pin       = GPIO_PIN_2 | GPIO_PIN_3;       // PC2 IO2, PC3 IO0
  gpio.Alternate = GPIO_AF9_OCTOSPI1;
  HAL_GPIO_Init(GPIOC, &gpio);

  gpio.Pin       = GPIO_PIN_10;                   // PB10 NCS (풀업: 초기화 전에도 CS 가 HIGH 로 있게)
  gpio.Pull      = GPIO_PULLUP;
  gpio.Alternate = GPIO_AF9_OCTOSPI1;
  HAL_GPIO_Init(GPIOB, &gpio);
}

#endif
