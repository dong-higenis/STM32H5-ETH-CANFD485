/*
 * uart.c  (앱용)
 *
 *  _DEF_UART1 = USART1  : 디버그 커넥터 J1 (로그, CLI, Step 10 부터 GUI 패킷도)
 *    - 보드는 TX/RX 가 반대로 연결돼 있어서 "SWAP" 기능으로 내부에서 바꿔준다.
 *    - 송신: 인터럽트 + 뮤텍스 ("마이크는 한 명씩").
 *            tx 버퍼가 꽉 차면 버리지 않고 빌 때까지 잠깐씩 쉬며 기다린다 (로그/패킷이 잘리면 안 되므로).
 *            RTOS 가 돌기 전(부팅 메시지)에는 폴링으로 보낸다.
 *
 *  _DEF_UART2 = USART11 : RS485 (PA6 TX, PA7 RX, PC1 DE)
 *    - DE 핀(보내는 중 = HIGH)을 UART 하드웨어가 알아서 켜고 끈다. 마지막 stop 비트까지.
 *    - 송신: 인터럽트. 보낼 바이트를 tx 버퍼에 넣어 두면, "다음 바이트 넣어도 돼" 인터럽트(TXE)마다
 *            하나씩 내보낸다. -> 9600bps 로 256바이트(약 270ms)를 보내도 net 태스크가 기다리지 않는다.
 *
 *  수신(둘 다): RXNE 인터럽트 -> qbuffer
 */
#include "uart.h"
#include "qbuffer.h"

#ifdef _USE_HW_UART


#define UART_RX_BUF_LENGTH      2048
#define UART_TX_BUF_LENGTH      2048      // 인터럽트 송신(RS485)용


typedef struct
{
  bool               is_open;
  bool               is_tx_irq;          // true = 인터럽트로 보내는 채널
  bool               is_tx_wait;         // true = tx 버퍼가 차면 기다린다 (false = 넘치는 만큼 버린다)
  uint32_t           baud;
  UART_HandleTypeDef h_uart;
  qbuffer_t          qbuffer;
  uint8_t            rx_buf[UART_RX_BUF_LENGTH];
  qbuffer_t          tx_q;
  uint8_t            tx_buf[UART_TX_BUF_LENGTH];
  uint32_t           rx_overrun;
  uint32_t           rx_error;
  uint32_t           rx_count;
  uint32_t           tx_count;
  uint32_t           tx_drop;
  SemaphoreHandle_t  tx_mutex;
} uart_tbl_t;


static uart_tbl_t uart_tbl[UART_MAX_CH];


static void uartISR(uint8_t ch);
static bool uartOpenDebug(uart_tbl_t *p_uart, uint32_t baud);
static bool uartOpenRs485(uart_tbl_t *p_uart, uint32_t baud);


bool uartInit(void)
{
  for (int i = 0; i < UART_MAX_CH; i++)
  {
    memset(&uart_tbl[i], 0, sizeof(uart_tbl_t));
    uart_tbl[i].baud     = 115200;
    uart_tbl[i].tx_mutex = xSemaphoreCreateMutex();   // RTOS 시작 전에도 만들 수 있다
    qbufferCreate(&uart_tbl[i].qbuffer, uart_tbl[i].rx_buf, UART_RX_BUF_LENGTH);
    qbufferCreate(&uart_tbl[i].tx_q, uart_tbl[i].tx_buf, UART_TX_BUF_LENGTH);
  }
  return true;
}

bool uartOpen(uint8_t ch, uint32_t baud)
{
  uart_tbl_t *p_uart;
  bool        ret = false;

  if (ch >= UART_MAX_CH) return false;
  p_uart = &uart_tbl[ch];

  if (p_uart->is_open)
    uartClose(ch);

  qbufferFlush(&p_uart->qbuffer);
  qbufferFlush(&p_uart->tx_q);

  switch (ch)
  {
    case _DEF_UART1: ret = uartOpenDebug(p_uart, baud); break;
    case _DEF_UART2: ret = uartOpenRs485(p_uart, baud); break;
  }

  if (ret)
  {
    p_uart->baud    = baud;
    p_uart->is_open = true;
  }
  return ret;
}

static bool uartOpenDebug(uart_tbl_t *p_uart, uint32_t baud)
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

  p_uart->is_tx_irq                          = true;
  p_uart->is_tx_wait                         = true;
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

  if (HAL_UART_Init(&p_uart->h_uart) != HAL_OK)
    return false;

  __HAL_UART_ENABLE_IT(&p_uart->h_uart, UART_IT_RXNE);
  HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
  return true;
}

static bool uartOpenRs485(uart_tbl_t *p_uart, uint32_t baud)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_USART11_CLK_ENABLE();   // 커널 클럭: 리셋 기본값 PCLK1(250MHz)
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  // PA6 TX, PA7 RX (AF7) - 기존 시험 펌웨어가 이 보드에서 쓴 설정
  gpio.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = GPIO_PULLUP;
  gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF7_USART11;
  HAL_GPIO_Init(GPIOA, &gpio);

  // PC1 DE (AF7): HIGH = 보내기, LOW = 받기. /RE 와 묶여 있다. 풀다운 = 평소에는 받기
  gpio.Pin  = GPIO_PIN_1;
  gpio.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOC, &gpio);

  p_uart->is_tx_irq                  = true;
  p_uart->is_tx_wait                 = false;   // RS485 는 net 태스크를 붙잡지 않게 넘치면 버린다
  p_uart->h_uart.Instance            = USART11;
  p_uart->h_uart.Init.BaudRate       = baud;
  p_uart->h_uart.Init.WordLength     = UART_WORDLENGTH_8B;
  p_uart->h_uart.Init.StopBits       = UART_STOPBITS_1;
  p_uart->h_uart.Init.Parity         = UART_PARITY_NONE;
  p_uart->h_uart.Init.Mode           = UART_MODE_TX_RX;
  p_uart->h_uart.Init.HwFlowCtl      = UART_HWCONTROL_NONE;
  p_uart->h_uart.Init.OverSampling   = UART_OVERSAMPLING_16;
  p_uart->h_uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  p_uart->h_uart.Init.ClockPrescaler = UART_PRESCALER_DIV1;

  // RS485 모드: DE 를 하드웨어가 제어. 앞뒤 여유 시간 1 (샘플 시간 단위)
  if (HAL_RS485Ex_Init(&p_uart->h_uart, UART_DE_POLARITY_HIGH, 1, 1) != HAL_OK)
    return false;

  __HAL_UART_ENABLE_IT(&p_uart->h_uart, UART_IT_RXNE);
  HAL_NVIC_SetPriority(USART11_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(USART11_IRQn);
  return true;
}

bool uartClose(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].is_open)
  {
    HAL_NVIC_DisableIRQ(ch == _DEF_UART1 ? USART1_IRQn : USART11_IRQn);
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

// tx 버퍼에 넣을 수 있는 만큼 넣고 TXE 인터럽트를 켠다. 넣은 바이트 수를 돌려준다
static uint32_t uartTxPush(uart_tbl_t *p_uart, const uint8_t *p_data, uint32_t length)
{
  uint32_t space;
  uint32_t ret;

  taskENTER_CRITICAL();
  space = (UART_TX_BUF_LENGTH - 1) - qbufferAvailable(&p_uart->tx_q);
  ret   = cmin(length, space);
  qbufferWrite(&p_uart->tx_q, p_data, ret);
  __HAL_UART_ENABLE_IT(&p_uart->h_uart, UART_IT_TXE);
  taskEXIT_CRITICAL();

  return ret;
}

uint32_t uartWrite(uint8_t ch, const uint8_t *p_data, uint32_t length)
{
  uart_tbl_t *p_uart;
  uint32_t    ret = 0;

  if (ch >= UART_MAX_CH || uart_tbl[ch].is_open != true) return 0;
  p_uart = &uart_tbl[ch];

  // ---- RTOS 가 돌기 전 : 폴링 (태스크가 하나뿐이라 순서를 기다릴 필요도 없다) ----
  if (rtosIsRunning() != true)
  {
    if (HAL_UART_Transmit(&p_uart->h_uart, p_data, length, 100) == HAL_OK)
      ret = length;
    return ret;
  }

  // ---- 버리는 채널 (RS485) : 넣을 수 있는 만큼만 ----
  if (p_uart->is_tx_wait != true)
  {
    ret = uartTxPush(p_uart, p_data, length);
    p_uart->tx_drop += length - ret;
    return ret;
  }

  // ---- 기다리는 채널 (디버그) : 다 넣을 때까지 1ms 씩 쉬며 넣는다 ----
  //   뮤텍스를 쥐고 있으므로 다른 태스크의 글자가 중간에 끼어들지 않는다 (패킷이 섞이지 않음)
  //   최대 기다림: 앞에 쌓인 버퍼 + 이 길이를 보내는 데 걸리는 시간 + 100ms
  uint32_t limit_ms = ((UART_TX_BUF_LENGTH + length) * 10 * 1000) / p_uart->baud + 100;
  uint32_t pre_time = millis();

  xSemaphoreTake(p_uart->tx_mutex, portMAX_DELAY);       // 마이크 잡기
  while (ret < length)
  {
    ret += uartTxPush(p_uart, &p_data[ret], length - ret);
    if (ret < length)
    {
      if (millis() - pre_time >= limit_ms)
        break;
      delay(1);                                          // 인터럽트가 비워 줄 때까지 다른 태스크에게 양보
    }
  }
  xSemaphoreGive(p_uart->tx_mutex);                      // 마이크 내려놓기

  p_uart->tx_drop += length - ret;
  return ret;
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

bool uartGetStat(uint8_t ch, uart_stat_t *p_stat)
{
  if (ch >= UART_MAX_CH) return false;
  p_stat->rx_count   = uart_tbl[ch].rx_count;
  p_stat->tx_count   = uart_tbl[ch].tx_count;
  p_stat->rx_overrun = uart_tbl[ch].rx_overrun;
  p_stat->rx_error   = uart_tbl[ch].rx_error;
  p_stat->tx_drop    = uart_tbl[ch].tx_drop;
  return true;
}


static void uartISR(uint8_t ch)
{
  uart_tbl_t    *p_uart = &uart_tbl[ch];
  USART_TypeDef *p_reg  = p_uart->h_uart.Instance;
  uint32_t       isr    = p_reg->ISR;

  // 받기
  if (isr & USART_ISR_RXNE_RXFNE)
  {
    uint8_t data = (uint8_t)p_reg->RDR;     // RDR 을 읽으면 RXNE 가 자동으로 지워진다
    if (qbufferWrite(&p_uart->qbuffer, &data, 1))
      p_uart->rx_count++;
  }

  // 오류 플래그는 직접 지워야 한다. 안 지우면 인터럽트가 계속 들어와 메인이 멈춘다.
  if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE))
  {
    if (isr & USART_ISR_ORE) p_uart->rx_overrun++;
    else                     p_uart->rx_error++;
    p_reg->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
  }

  // 보내기 (인터럽트 송신 중일 때만): 송신 레지스터가 비었으면 다음 바이트
  if (p_uart->is_tx_irq && (p_reg->CR1 & USART_CR1_TXEIE_TXFNFIE) && (isr & USART_ISR_TXE_TXFNF))
  {
    uint8_t data;

    if (qbufferRead(&p_uart->tx_q, &data, 1))
    {
      p_reg->TDR = data;
      p_uart->tx_count++;
    }
    else
    {
      p_reg->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;   // 다 보냈으면 인터럽트 끄기 (DE 는 하드웨어가 내림)
    }
  }
}

void USART1_IRQHandler(void)
{
  uartISR(_DEF_UART1);
}

void USART11_IRQHandler(void)
{
  uartISR(_DEF_UART2);
}

#endif
