/*
 * can.c  -  CAN-FD 드라이버 (FDCAN1)
 *
 *  핀:  PB7 TX, PB8 RX (AF9)   /   PB6 = 트랜시버 STB (HIGH = 대기, LOW = 동작)
 *  클럭: FDCAN 전용 클럭 80MHz (PLL2Q, bsp.c)
 *
 *  비트 타이밍은 기존 시험 펌웨어가 이 보드에서 확인한 값과 같다.
 *    중재(nominal) : 1 비트 = 16 tq(time quanta), 샘플 위치 87.5%
 *    데이터(data)  : 1 비트 = 20 tq, 샘플 위치 80%  (5M 은 16 tq 75%, 100K 는 25 tq)
 *
 *  받기: 인터럽트가 RX FIFO0 에서 꺼내 RX 큐(rx_q)에 넣는다 -> net 태스크가 꺼내 PC 로 보낸다.
 *  보내기: 칩의 TX FIFO 는 3개뿐이다. PC 는 그보다 빨리 보낼 수 있다.
 *          그래서 앞에 64개짜리 TX 큐(tx_q)를 두고,
 *          칩 FIFO 가 비면(인터럽트) TX 큐에서 꺼내 채운다. -> 몰려와도 버리지 않는다.
 *
 *  자동 재전송: 켠다 (데이터 손실 점검에서 바꿈).
 *    끄면, 우선순위 경쟁에서 지거나 오류가 난 프레임이 한 번만 보내지고 조용히 사라진다.
 *    켜면 성공할 때까지 칩이 다시 보낸다. 다만 받아 주는 장치(ACK)가 없으면 영원히 다시 보내므로,
 *    100ms 동안 한 프레임도 못 내보내면 TX FIFO 의 프레임을 포기하고 tx_timeout 으로 센다 (canUpdate).
 *
 *  손실을 세는 곳 (can.h 의 can_info_t 설명 참고)
 *    보내기: TX 큐 꽉 참(tx_fail), 포기(tx_timeout). 실제로 나간 수는 tx_ok (송신 완료 인터럽트)
 *    받기:   칩 RX FIFO 넘침(rx_fifo_lost), RX 큐 꽉 참(rx_drop)
 *
 *  버스오프(오류가 너무 많아 칩이 스스로 버스에서 빠짐)가 되면 자동으로 다시 켠다.
 *  안 그러면 PC 가 CAN 을 다시 열 때까지 CAN 이 죽어 있다.
 */
#include "can.h"
#include "qbuffer.h"

#ifdef _USE_HW_CAN


static const uint8_t  dlc_len_tbl[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
static const uint32_t baud_tbl[8]     = {100000, 125000, 250000, 500000, 1000000, 2000000, 4000000, 5000000};

#define CAN_KERNEL_CLOCK      80000000U
#define CAN_TX_TIMEOUT_MS     100         // 이 시간 동안 한 프레임도 못 내보내면 TX FIFO 를 비운다
#define CAN_TX_BUF_ALL        (FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2)


static FDCAN_HandleTypeDef hcan;
static bool                is_open = false;
static CanMode_t           can_mode = CAN_NORMAL;
static CanFilterType_t     filter_type = CAN_ID_MASK;

static qbuffer_t           rx_q;
static can_msg_t           rx_buf[CAN_MSG_RX_BUF_MAX];
static qbuffer_t           tx_q;
static can_msg_t           tx_buf[CAN_MSG_TX_BUF_MAX];

static volatile uint32_t   rx_count = 0;
static volatile uint32_t   rx_drop  = 0;
static volatile uint32_t   tx_count = 0;
static volatile uint32_t   tx_fail  = 0;
static volatile uint32_t   tx_ok    = 0;
static volatile uint32_t   tx_timeout   = 0;
static volatile uint32_t   rx_fifo_lost = 0;
static volatile uint32_t   bus_off_count = 0;
static volatile uint32_t   tx_progress_ms = 0;   // 송신이 마지막으로 "진행"된 시각 (나감 또는 새로 시작)

// TX 버퍼 3개 중 "프레임을 넣었고 아직 결과를 안 센" 버퍼 (비트 0~2)
//   전송이 끝나면(TXBRP=0) 칩의 표시를 보고 센다: TXBTO(나감) -> tx_ok, 아니면(포기) -> tx_timeout
//   HAL 의 송신 완료 콜백은 쓰지 않는다. 한 인터럽트 안에서 "TX FIFO 빔"을 먼저 처리해
//   새 프레임이 들어가면 표시가 지워져서, 콜백으로는 정확히 셀 수 없었다 (보드 시험에서 확인).
static volatile uint32_t   tx_in_flight = 0;


static bool canTiming(CanBaud_t baud, bool is_data, uint32_t *p_pre, uint32_t *p_seg1, uint32_t *p_seg2);
static bool canHwWrite(const can_msg_t *p_msg);
static void canTxKick(void);
static void canTxAccount(void);
static uint32_t bitCount(uint32_t bits);


bool canInit(void)
{
  GPIO_InitTypeDef gpio = {0};

  qbufferCreateBySize(&rx_q, (uint8_t *)rx_buf, sizeof(can_msg_t), CAN_MSG_RX_BUF_MAX);
  qbufferCreateBySize(&tx_q, (uint8_t *)tx_buf, sizeof(can_msg_t), CAN_MSG_TX_BUF_MAX);

  __HAL_RCC_GPIOB_CLK_ENABLE();

  // STB: 처음엔 대기(HIGH). 버스에 아무 신호도 내보내지 않는다
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
  gpio.Pin   = GPIO_PIN_6;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);

  gpio.Pin       = GPIO_PIN_7 | GPIO_PIN_8;
  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = GPIO_PULLUP;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF9_FDCAN1;
  HAL_GPIO_Init(GPIOB, &gpio);

  __HAL_RCC_FDCAN_CLK_ENABLE();
  return true;
}

bool canOpen(uint8_t ch, CanMode_t mode, CanFrame_t frame, CanBaud_t baud, CanBaud_t baud_data)
{
  uint32_t np, ns1, ns2, dp, ds1, ds2;

  if (ch >= CAN_MAX_CH || mode > CAN_LOOPBACK || frame > CAN_FD_BRS)
    return false;
  if (baud > CAN_1M || baud_data > CAN_5M)            // 중재 속도는 1M 까지
    return false;
  if (!canTiming(baud, false, &np, &ns1, &ns2) || !canTiming(baud_data, true, &dp, &ds1, &ds2))
    return false;

  canClose(ch);

  memset(&hcan, 0, sizeof(hcan));
  hcan.Instance                  = FDCAN1;
  hcan.Init.ClockDivider         = FDCAN_CLOCK_DIV1;
  hcan.Init.FrameFormat          = (frame == CAN_CLASSIC)   ? FDCAN_FRAME_CLASSIC :
                                   (frame == CAN_FD_NO_BRS) ? FDCAN_FRAME_FD_NO_BRS : FDCAN_FRAME_FD_BRS;
  hcan.Init.Mode                 = (mode == CAN_LOOPBACK) ? FDCAN_MODE_INTERNAL_LOOPBACK :
                                   (mode == CAN_MONITOR)  ? FDCAN_MODE_BUS_MONITORING   : FDCAN_MODE_NORMAL;
  hcan.Init.AutoRetransmission   = ENABLE;     // 실패하면 칩이 다시 보낸다 (포기는 canUpdate 가)
  hcan.Init.TransmitPause        = ENABLE;
  hcan.Init.ProtocolException    = ENABLE;
  hcan.Init.NominalPrescaler     = np;
  hcan.Init.NominalSyncJumpWidth = ns2;
  hcan.Init.NominalTimeSeg1      = ns1;
  hcan.Init.NominalTimeSeg2      = ns2;
  hcan.Init.DataPrescaler        = dp;
  hcan.Init.DataSyncJumpWidth    = ds2;
  hcan.Init.DataTimeSeg1         = ds1;
  hcan.Init.DataTimeSeg2         = ds2;
  hcan.Init.StdFiltersNbr        = 1;
  hcan.Init.ExtFiltersNbr        = 1;
  hcan.Init.TxFifoQueueMode      = FDCAN_TX_FIFO_OPERATION;

  if (HAL_FDCAN_Init(&hcan) != HAL_OK)
    return false;

  // FD+BRS 로 빠르게 보낼 때, 트랜시버를 거쳐 돌아오는 지연을 보정한다
  if (mode == CAN_NORMAL && frame == CAN_FD_BRS && baud_data >= CAN_2M)
  {
    HAL_FDCAN_ConfigTxDelayCompensation(&hcan, dp * ds1, 0);
    HAL_FDCAN_EnableTxDelayCompensation(&hcan);
  }

  // 기본 필터: 모두 받기 (ID 0, 마스크 0)
  canConfigFilter(ch, 0, CAN_STD, 0, 0);

  // 인터럽트: 새 프레임, 칩 RX FIFO 넘침, TX FIFO 빔(채우기), 버스오프
  HAL_FDCAN_ActivateNotification(&hcan,
                                 FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST |
                                 FDCAN_IT_TX_FIFO_EMPTY | FDCAN_IT_BUS_OFF,
                                 0);
  HAL_NVIC_SetPriority(FDCAN1_IT0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(FDCAN1_IT0_IRQn);

  if (HAL_FDCAN_Start(&hcan) != HAL_OK)
  {
    HAL_FDCAN_DeInit(&hcan);
    return false;
  }

  qbufferFlush(&rx_q);
  qbufferFlush(&tx_q);
  tx_in_flight = 0;
  can_mode = mode;
  is_open  = true;

  // 내부 루프백은 칩 안에서만 돈다 -> 트랜시버는 대기로 두어 버스를 건드리지 않는다
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, (mode == CAN_LOOPBACK) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  return true;
}

void canClose(uint8_t ch)
{
  (void)ch;

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);   // 트랜시버 대기
  if (is_open)
  {
    HAL_NVIC_DisableIRQ(FDCAN1_IT0_IRQn);
    canTxAccount();
    // 닫을 때 아직 못 보낸 것(칩 TX FIFO + TX 큐)는 포기로 센다
    tx_timeout += bitCount(tx_in_flight) + qbufferAvailable(&tx_q);
    tx_in_flight = 0;
    qbufferFlush(&tx_q);
    HAL_FDCAN_Stop(&hcan);
    HAL_FDCAN_DeInit(&hcan);
  }
  is_open = false;
}

bool canIsOpen(uint8_t ch)
{
  (void)ch;
  return is_open;
}

uint8_t canDlcToLength(CanDlc_t dlc)
{
  return dlc_len_tbl[dlc & 0x0F];
}

bool canMsgInit(can_msg_t *p_msg, CanFrame_t frame, CanIdType_t id_type, CanDlc_t dlc)
{
  p_msg->frame   = frame;
  p_msg->id_type = id_type;
  p_msg->dlc     = (CanDlc_t)(dlc & 0x0F);
  p_msg->length  = dlc_len_tbl[p_msg->dlc];
  return true;
}

bool canMsgWrite(uint8_t ch, can_msg_t *p_msg)
{
  bool ret;

  (void)ch;
  if (is_open != true || can_mode == CAN_MONITOR)    // 모니터 모드는 듣기만 한다
  {
    tx_fail++;
    return false;
  }
  // Classic CAN 은 8바이트까지
  if (p_msg->frame == CAN_CLASSIC && p_msg->dlc > CAN_DLC_8)
  {
    tx_fail++;
    return false;
  }

  // TX 큐에 넣고, 칩 FIFO 에 빈 슬롯이 있으면 바로 넣는다.
  // 인터럽트도 TX 큐를 꺼내므로, 그동안 CAN 인터럽트를 잠깐 막는다 (우선순위 5 이하 차단)
  taskENTER_CRITICAL();
  ret = qbufferWrite(&tx_q, (uint8_t *)p_msg, 1);
  canTxKick();
  taskEXIT_CRITICAL();

  if (ret)
    tx_count++;
  else
    tx_fail++;                                         // TX 큐도 꽉 참
  return ret;
}

// TX 큐 -> 칩 FIFO (빈 슬롯만큼). 인터럽트 안이나 잠금 안에서만 부른다
static void canTxKick(void)
{
  can_msg_t msg;

  canTxAccount();           // 끝난 버퍼를 먼저 센다 (곧 새 프레임으로 덮어쓰므로)

  while (qbufferAvailable(&tx_q) > 0 && HAL_FDCAN_GetTxFifoFreeLevel(&hcan) > 0)
  {
    // TX FIFO 가 비어 있다가 새로 시작하면, 시간초과 시계도 지금부터
    if ((hcan.Instance->TXBRP & CAN_TX_BUF_ALL) == 0)
      tx_progress_ms = millis();

    qbufferRead(&tx_q, (uint8_t *)&msg, 1);
    if (canHwWrite(&msg) != true)
      tx_timeout++;                                    // 칩에 못 넣음 (거의 없음). TX 큐에선 빠졌으니 잃은 것으로 센다
  }
}

static uint32_t bitCount(uint32_t bits)
{
  uint32_t n = 0;
  for (; bits; bits &= bits - 1)
    n++;
  return n;
}

// 끝난 버퍼(요청 중 표시 TXBRP 가 꺼짐)의 결과를 센다. 버퍼마다 한 번만.
//   인터럽트 안이나 잠금(critical) 안에서만 부른다.
static void canTxAccount(void)
{
  uint32_t done = tx_in_flight & ~(hcan.Instance->TXBRP & CAN_TX_BUF_ALL);

  if (done == 0)
    return;

  uint32_t ok = done & hcan.Instance->TXBTO;          // 나감
  tx_ok      += bitCount(ok);
  tx_timeout += bitCount(done & ~ok);                 // 나가지 못하고 끝남 = 포기
  if (ok)
    tx_progress_ms = millis();
  tx_in_flight &= ~done;
}

// 1ms 마다 (net 태스크): 끝난 버퍼를 세고, TX FIFO 가 100ms 동안 꼼짝 안 하면 포기시킨다.
// 받아 주는 장치가 없을 때(ACK 없음) 칩이 영원히 다시 보내며 TX 큐가 막히는 것을 막는다.
void canUpdate(uint8_t ch)
{
  uint32_t pending;

  (void)ch;
  if (is_open != true)
    return;

  taskENTER_CRITICAL();
  canTxAccount();
  pending = hcan.Instance->TXBRP & CAN_TX_BUF_ALL;
  if (pending != 0 && millis() - tx_progress_ms >= CAN_TX_TIMEOUT_MS)
  {
    HAL_FDCAN_AbortTxRequest(&hcan, pending);          // 포기가 끝나면 canTxAccount 가 timeout 으로 센다
    tx_progress_ms = millis();
  }
  taskEXIT_CRITICAL();
}

// 버스오프 (인터럽트 안): 칩이 스스로 멈췄다(INIT=1). INIT 을 풀면 버스가 조용해진 뒤 다시 참여한다.
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t ErrorStatusITs)
{
  if ((ErrorStatusITs & FDCAN_IT_BUS_OFF) && (hfdcan->Instance->PSR & FDCAN_PSR_BO))
  {
    bus_off_count++;
    hfdcan->Instance->CCCR &= ~FDCAN_CCCR_INIT;
  }
}

static bool canHwWrite(const can_msg_t *p_msg)
{
  FDCAN_TxHeaderTypeDef header = {0};

  header.Identifier          = p_msg->id & ((p_msg->id_type == CAN_EXT) ? 0x1FFFFFFF : 0x7FF);
  header.IdType              = (p_msg->id_type == CAN_EXT) ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
  header.TxFrameType         = FDCAN_DATA_FRAME;
  header.DataLength          = p_msg->dlc;                 // H5 HAL: DLC 번호 0~15 를 그대로
  header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  header.BitRateSwitch       = (p_msg->frame == CAN_FD_BRS) ? FDCAN_BRS_ON : FDCAN_BRS_OFF;
  header.FDFormat            = (p_msg->frame == CAN_CLASSIC) ? FDCAN_CLASSIC_CAN : FDCAN_FD_CAN;
  header.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
  header.MessageMarker       = 0;

  // 이제 덮어쓸 버퍼(put index)의 이전 결과를 먼저 센다. 이 버퍼는 비어 있는 것이 확실해서 표시를 정확히 읽을 수 있다.
  // (canTxAccount 를 부른 뒤 넣기 직전에 막 끝난 버퍼면, 여기서 안 세면 결과가 지워져 못 센다)
  uint32_t put = 1U << ((hcan.Instance->TXFQS & FDCAN_TXFQS_TFQPI) >> FDCAN_TXFQS_TFQPI_Pos);
  if (tx_in_flight & put)
  {
    if (hcan.Instance->TXBTO & put)
    {
      tx_ok++;
      tx_progress_ms = millis();
    }
    else
    {
      tx_timeout++;
    }
    tx_in_flight &= ~put;
  }

  if (HAL_FDCAN_AddMessageToTxFifoQ(&hcan, &header, (uint8_t *)p_msg->data) != HAL_OK)
    return false;

  // 이 버퍼에 프레임이 들어갔다 -> 끝나면 canTxAccount 가 센다
  tx_in_flight |= HAL_FDCAN_GetLatestTxFifoQRequestBuffer(&hcan);
  return true;
}

// 칩 TX FIFO 가 비었다 (인터럽트 안) -> TX 큐에서 채운다
void HAL_FDCAN_TxFifoEmptyCallback(FDCAN_HandleTypeDef *hfdcan)
{
  (void)hfdcan;
  canTxKick();
}

// TX 큐 빈 슬롯 수. net 태스크가 0 이면 PC 패킷을 더 꺼내지 않는다 (흐름 제어)
uint32_t canMsgTxFree(uint8_t ch)
{
  (void)ch;
  return (CAN_MSG_TX_BUF_MAX - 1) - qbufferAvailable(&tx_q);   // qbuffer 는 슬롯 하나를 비워 둔다
}

uint32_t canMsgAvailable(uint8_t ch)
{
  (void)ch;
  return qbufferAvailable(&rx_q);
}

bool canMsgRead(uint8_t ch, can_msg_t *p_msg)
{
  (void)ch;
  return qbufferRead(&rx_q, (uint8_t *)p_msg, 1);
}

bool canSetFilterType(CanFilterType_t type)
{
  filter_type = type;
  return true;
}

/*
 * 필터: 고른 ID 종류(표준/확장)에만 적용한다. 다른 종류는 모두 받는다.
 *   MASK  : (받은 ID & id2) == (id1 & id2) 이면 받기.  id2 = 0 이면 전부 받기
 *   RANGE : id1 <= 받은 ID <= id2 이면 받기
 */
bool canConfigFilter(uint8_t ch, uint8_t index, CanIdType_t id_type, uint32_t id1, uint32_t id2)
{
  FDCAN_FilterTypeDef filter = {0};
  bool                was_running;
  bool                ret;

  (void)ch;
  (void)index;
  if (hcan.State != HAL_FDCAN_STATE_READY && hcan.State != HAL_FDCAN_STATE_BUSY)
    return false;

  // "필터에 안 맞는 것" 설정은 멈춘 상태에서만 바꿀 수 있다 -> 돌고 있으면 잠깐 멈춘다
  was_running = (hcan.State == HAL_FDCAN_STATE_BUSY);
  if (was_running)
    HAL_FDCAN_Stop(&hcan);

  filter.IdType       = (id_type == CAN_EXT) ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
  filter.FilterIndex  = 0;
  filter.FilterType   = (filter_type == CAN_ID_RANGE) ? FDCAN_FILTER_RANGE : FDCAN_FILTER_MASK;
  filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
  filter.FilterID1    = id1;
  filter.FilterID2    = id2;
  ret = (HAL_FDCAN_ConfigFilter(&hcan, &filter) == HAL_OK);

  // 필터에 안 맞는 것: 고른 종류는 버리고, 다른 종류는 받는다. 리모트 프레임은 버린다.
  ret &= (HAL_FDCAN_ConfigGlobalFilter(&hcan,
                                       (id_type == CAN_STD) ? FDCAN_REJECT : FDCAN_ACCEPT_IN_RX_FIFO0,
                                       (id_type == CAN_EXT) ? FDCAN_REJECT : FDCAN_ACCEPT_IN_RX_FIFO0,
                                       FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE) == HAL_OK);

  if (was_running)
    HAL_FDCAN_Start(&hcan);
  return ret;
}

bool canGetInfo(uint8_t ch, can_info_t *p_info)
{
  FDCAN_ErrorCountersTypeDef  err    = {0};
  FDCAN_ProtocolStatusTypeDef status = {0};

  (void)ch;
  if (is_open)
  {
    taskENTER_CRITICAL();
    canTxAccount();         // 끝났는데 아직 안 센 버퍼가 없게
    taskEXIT_CRITICAL();
  }

  memset(p_info, 0, sizeof(can_info_t));
  p_info->is_open  = is_open;
  p_info->mode     = can_mode;
  p_info->rx_count = rx_count;
  p_info->tx_count = tx_count;
  p_info->tx_fail  = tx_fail;
  p_info->rx_drop  = rx_drop;
  p_info->tx_ok         = tx_ok;
  p_info->tx_timeout    = tx_timeout;
  p_info->rx_fifo_lost  = rx_fifo_lost;
  p_info->bus_off_count = bus_off_count;
  p_info->tx_pending    = qbufferAvailable(&tx_q);
  if (is_open)
    p_info->tx_pending += bitCount(hcan.Instance->TXBRP & CAN_TX_BUF_ALL);

  if (is_open)
  {
    HAL_FDCAN_GetErrorCounters(&hcan, &err);
    HAL_FDCAN_GetProtocolStatus(&hcan, &status);
    p_info->tec        = err.TxErrorCnt;
    p_info->rec        = err.RxErrorCnt;
    p_info->bus_off    = status.BusOff != 0;
    p_info->last_error = status.LastErrorCode;
  }
  return true;
}

void canClearCount(uint8_t ch)
{
  (void)ch;
  taskENTER_CRITICAL();
  rx_count = rx_drop = tx_count = tx_fail = 0;
  tx_ok = tx_timeout = rx_fifo_lost = bus_off_count = 0;
  taskEXIT_CRITICAL();
}


/*
 * 비트 타이밍 계산
 *   1 비트 시간 = (1 + seg1 + seg2) tq,  1 tq = prescaler / 80MHz
 *   예) 500K 중재: 80MHz / 500K / 16tq = prescaler 10, seg1 13, seg2 2 -> 샘플 위치 (1+13)/16 = 87.5%
 */
static bool canTiming(CanBaud_t baud, bool is_data, uint32_t *p_pre, uint32_t *p_seg1, uint32_t *p_seg2)
{
  uint32_t rate = baud_tbl[baud];
  uint32_t quanta;

  if (is_data)
    quanta = (baud == CAN_100K) ? 25 : (baud == CAN_5M) ? 16 : 20;
  else
    quanta = 16;

  *p_pre  = CAN_KERNEL_CLOCK / rate / quanta;
  *p_seg2 = is_data ? ((baud == CAN_100K) ? 5 : 4) : 2;
  *p_seg1 = quanta - 1 - *p_seg2;

  return (*p_pre >= 1 && *p_pre * rate * quanta == CAN_KERNEL_CLOCK);
}


void FDCAN1_IT0_IRQHandler(void)
{
  HAL_FDCAN_IRQHandler(&hcan);
}

// 새 프레임이 FIFO0 에 들어오거나, FIFO0 이 넘쳤을 때 불린다 (인터럽트 안)
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
  FDCAN_RxHeaderTypeDef header;
  can_msg_t             msg;

  // 칩 RX FIFO(3개)가 꽉 찬 상태에서 또 와서 버려졌다
  if (RxFifo0ITs & FDCAN_IT_RX_FIFO0_MESSAGE_LOST)
    rx_fifo_lost++;

  if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) == 0)
    return;

  while (HAL_FDCAN_GetRxFifoFillLevel(hfdcan, FDCAN_RX_FIFO0) > 0)
  {
    if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &header, msg.data) != HAL_OK)
      break;

    msg.id        = header.Identifier;
    msg.id_type   = (header.IdType == FDCAN_EXTENDED_ID) ? CAN_EXT : CAN_STD;
    msg.dlc       = (CanDlc_t)(header.DataLength & 0x0F);
    msg.length    = dlc_len_tbl[msg.dlc];
    msg.frame     = (header.FDFormat == FDCAN_CLASSIC_CAN) ? CAN_CLASSIC :
                    (header.BitRateSwitch == FDCAN_BRS_ON) ? CAN_FD_BRS : CAN_FD_NO_BRS;
    msg.timestamp = millis();

    rx_count++;
    if (qbufferWrite(&rx_q, (uint8_t *)&msg, 1) != true)
      rx_drop++;
  }
}

#endif
