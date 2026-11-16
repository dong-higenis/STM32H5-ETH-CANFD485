/**
 * @brief 부트로더 전용 uart
 */
#include "uart.h"
#include "qbuffer.h"

#ifdef _USE_HW_UART


#define UART_RX_BUF_LENGTH      1024


typedef struct
{
  bool               is_open;
  uint32_t           baud;
  UART_HandleTypeDef h_uart;
  qbuffer_t          qbuffer;
  uint8_t            rx_buf[UART_RX_BUF_LENGTH];
  uint32_t           rx_overrun;
} uart_tbl_t;


static uart_tbl_t uart_tbl[UART_MAX_CH];


static void uartRxISR(uint8_t ch);


bool uartInit(void)
{
  for (int i = 0; i < UART_MAX_CH; i++)
  {
    uart_tbl[i].is_open    = false;
    uart_tbl[i].baud       = 115200;
    uart_tbl[i].rx_overrun = 0;
  }
  return true;
}

bool uartOpen(uint8_t ch, uint32_t baud)
{
  bool ret = false;
  uart_tbl_t *p_uart;

  if (ch >= UART_MAX_CH) return false;
  p_uart = &uart_tbl[ch];

  switch (ch)
  {
    case _DEF_UART1:
    {
      GPIO_InitTypeDef gpio = {0};

      __HAL_RCC_USART1_CLK_ENABLE();    // 커널 클럭: 리셋 기본값 PCLK2(250MHz)
      __HAL_RCC_GPIOB_CLK_ENABLE();

      gpio.Pin       = GPIO_PIN_14 | GPIO_PIN_15;
      gpio.Mode      = GPIO_MODE_AF_PP;
      gpio.Pull      = GPIO_PULLUP;
      gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
      gpio.Alternate = GPIO_AF4_USART1;
      HAL_GPIO_Init(GPIOB, &gpio);

      p_uart->h_uart.Instance                    = USART1;
      p_uart->h_uart.Init.BaudRate               = baud;
      p_uart->h_uart.Init.WordLength             = UART_WORDLENGTH_8B;
      p_uart->h_uart.Init.StopBits               = UART_STOPBITS_1;
      p_uart->h_uart.Init.Parity                 = UART_PARITY_NONE;
      p_uart->h_uart.Init.Mode                   = UART_MODE_TX_RX;
      p_uart->h_uart.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
      p_uart->h_uart.Init.OverSampling           = UART_OVERSAMPLING_16;
      p_uart->h_uart.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
      p_uart->h_uart.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
      p_uart->h_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_SWAP_INIT;
      p_uart->h_uart.AdvancedInit.Swap           = UART_ADVFEATURE_SWAP_ENABLE;

      qbufferCreate(&p_uart->qbuffer, p_uart->rx_buf, UART_RX_BUF_LENGTH);

      if (HAL_UART_Init(&p_uart->h_uart) == HAL_OK)
      {
        __HAL_UART_ENABLE_IT(&p_uart->h_uart, UART_IT_RXNE);
        HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);

        p_uart->baud    = baud;
        p_uart->is_open = true;
        ret = true;
      }
      break;
    }
  }

  return ret;
}

bool uartClose(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].is_open)
  {
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    HAL_UART_DeInit(&uart_tbl[ch].h_uart);
    uart_tbl[ch].is_open = false;
  }
  return true;
}

bool uartIsOpen(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;
  return uart_tbl[ch].is_open;
}

uint32_t uartAvailable(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;
  return qbufferAvailable(&uart_tbl[ch].qbuffer);
}

bool uartFlush(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;
  qbufferFlush(&uart_tbl[ch].qbuffer);
  return true;
}

uint8_t uartRead(uint8_t ch)
{
  uint8_t data = 0;

  if (ch >= UART_MAX_CH) return 0;
  qbufferRead(&uart_tbl[ch].qbuffer, &data, 1);
  return data;
}

uint32_t uartWrite(uint8_t ch, const uint8_t *p_data, uint32_t length)
{
  if (ch >= UART_MAX_CH || uart_tbl[ch].is_open != true) return 0;

  if (HAL_UART_Transmit(&uart_tbl[ch].h_uart, p_data, length, 100) == HAL_OK)
  {
    return length;
  }
  return 0;
}

uint32_t uartPrintf(uint8_t ch, const char *fmt, ...)
{
  char buf[256];
  va_list args;
  int len;

  va_start(args, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (len <= 0) return 0;
  return uartWrite(ch, (uint8_t *)buf, cmin((uint32_t)len, sizeof(buf) - 1));
}

uint32_t uartGetBaud(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;
  return uart_tbl[ch].baud;
}


static void uartRxISR(uint8_t ch)
{
  USART_TypeDef *p_reg = uart_tbl[ch].h_uart.Instance;
  uint32_t isr = p_reg->ISR;

  if (isr & USART_ISR_RXNE_RXFNE)
  {
    uint8_t data = (uint8_t)p_reg->RDR;     // RDR 을 읽으면 RXNE 가 자동으로 지워진다
    qbufferWrite(&uart_tbl[ch].qbuffer, &data, 1);
  }

  // 오류 플래그는 직접 지워야 한다. 안 지우면 인터럽트가 계속 들어와 메인이 멈춘다.
  if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE))
  {
    if (isr & USART_ISR_ORE) uart_tbl[ch].rx_overrun++;
    p_reg->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
  }
}

void USART1_IRQHandler(void)
{
  uartRxISR(_DEF_UART1);
}

#endif
