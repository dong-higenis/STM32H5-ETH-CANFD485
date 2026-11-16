#include "cmd_uart.h"
#include "stream_buffer.h"


/**
 * @brief J1 UART(COM 포트)로 오는 GUI 패킷 처리
 *
 *  J1 UART 하나를 CLI(사람이 치는 명령)와 GUI(바이너리 패킷)가 함께 쓴다.
 *
 *    cli 태스크                                  net 태스크
 *    ----------                                  ----------
 *    UART 에서 1바이트 읽음
 *      -> RX hook(cmdUartRxHook)이 먼저 본다
 *           02 로 시작하면 패킷 -> stream buffer ->  cmd_uart 로 꺼내서 처리, 응답은 UART 로
 *           아니면 CLI 로
 *
 *  net 태스크가 처리하는 이유:
 *    CAN, RS485, Flash 를 만지는 명령 처리를 한 태스크에 모아 두어야
 *    TCP 와 COM 에서 동시에 명령이 와도 서로 엉키지 않는다.
 *
 *  RX hook 이 패킷 끝을 아는 방법:
 *    패킷 헤더 9바이트 중 7, 8번째가 data 길이다. 전체 = 10 + 길이.
 */


#define CMD_STX0                0x02
#define CMD_STX1                0xFD
#define UART_PKT_TIMEOUT_MS     100     // 패킷 중간에 100ms 넘게 끊기면 CLI 입력으로 돌아간다 (cmd.c 와 같음)
#define UART_PKT_BOX_SIZE       2048    // RX hook -> net 태스크 stream buffer 크기 (가장 큰 패킷 1034 바이트 2개)


static StreamBufferHandle_t box = NULL;

static bool     in_packet = false;      // 지금 패킷을 받는 중인가
static uint32_t pkt_index;              // 패킷의 몇 번째 바이트인가
static uint32_t pkt_total;              // 패킷 전체 길이 (헤더를 다 받아야 안다)
static uint8_t  pkt_len_l;
static uint32_t pkt_time;
static volatile bool human_enter = false;
static uint32_t box_drop = 0;


static bool cmdUartRxHook(uint8_t data);


/**
 * @brief stream buffer 를 만들고 RX hook 을 CLI 에 등록한다.
 */
bool cmdUartInit(void)
{
  box = xStreamBufferCreate(UART_PKT_BOX_SIZE, 1);
  if (box == NULL)
    return false;

  cliSetRxHook(cmdUartRxHook);
  return true;
}

/**
 * @brief CLI 에서 Enter 가 들어왔는지 알려 주고, 플래그를 지운다.
 */
bool cmdUartTakeHumanEnter(void)
{
  bool ret = human_enter;

  human_enter = false;
  return ret;
}


/**
 * @brief RX hook: cli 태스크에서 UART 수신 바이트마다 불린다.
 * @return true = 패킷 바이트라서 가져감, false = CLI 로 넘김
 */
static bool cmdUartRxHook(uint8_t data)
{
  if (in_packet && millis() - pkt_time >= UART_PKT_TIMEOUT_MS)
  {
    in_packet = false;                  // 패킷이 중간에 끊겼다. 남은 바이트는 net 쪽 cmd.c 가 시간초과로 버린다
  }

  if (in_packet != true)
  {
    if (data != CMD_STX0)               // 02 는 키보드로 칠 일이 없는 글자(Ctrl+B)라서 패킷 시작으로 본다
    {
      if (data == '\r')
        human_enter = true;             // CLI 사용 시작 -> COM 쪽 CAN/RS485 정리 신호
      return false;
    }
    in_packet = true;
    pkt_index = 0;
    pkt_total = 10;                     // 헤더를 다 받기 전에는 최소 길이로 둔다
  }

  pkt_time = millis();

  if (xStreamBufferSend(box, &data, 1, 0) != 1)
    box_drop++;                         // stream buffer 가 꽉 참 (net 태스크가 멈춘 경우). 깨진 패킷은 체크섬 검사에서 걸러진다

  switch (pkt_index)
  {
    case 1:
      if (data != CMD_STX1)             // 02 다음이 FD 가 아니면 패킷이 아니었다
        in_packet = false;
      break;
    case 7:
      pkt_len_l = data;
      break;
    case 8:
    {
      uint32_t length = pkt_len_l | ((uint32_t)data << 8);

      if (length > CMD_MAX_DATA_LENGTH)
        pkt_total = 9;                  // 너무 긴 패킷: cmd.c 가 헤더까지만 읽고 에러로 응답한다
      else
        pkt_total = 10 + length;
      break;
    }
  }

  pkt_index++;
  if (pkt_index >= pkt_total)
    in_packet = false;                  // 패킷 1개 끝

  return true;
}


/**
 * @brief 여기부터 net 태스크 쪽: cmd 드라이버에 전달할 함수들
 */


/**
 * @brief UART 열기
 *        UART 자체는 CLI 가 이미 열어 두었으므로, stream buffer 가 있는지만 본다.
 */
static bool uartOpenCmd(void *args)
{
  (void)args;
  return (box != NULL);
}

/**
 * @brief UART 닫기 (CLI 가 계속 쓰므로 할 일이 없다)
 */
static bool uartCloseCmd(void *args)
{
  (void)args;
  return true;
}

/**
 * @brief stream buffer 에 쌓인 패킷 바이트 수
 */
static uint32_t uartAvailableCmd(void *args)
{
  (void)args;
  return xStreamBufferBytesAvailable(box);
}

/**
 * @brief stream buffer 를 비운다.
 */
static bool uartFlushCmd(void *args)
{
  (void)args;
  xStreamBufferReset(box);
  return true;
}

/**
 * @brief stream buffer 에서 1바이트를 꺼낸다. (비어 있으면 0)
 */
static uint8_t uartReadCmd(void *args)
{
  uint8_t data = 0;

  (void)args;
  xStreamBufferReceive(box, &data, 1, 0);
  return data;
}

/**
 * @brief 응답을 CLI 와 같은 UART 로 보낸다. (뮤텍스로 패킷 1개씩)
 */
static uint32_t uartWriteCmd(void *args, const uint8_t *p_data, uint32_t length)
{
  (void)args;
  return uartWrite(HW_CLI_CH, p_data, length);
}

/**
 * @brief cmd 드라이버의 함수 포인터 6개에 UART 함수들을 연결한다.
 */
bool cmdUartInitDriver(cmd_driver_t *p_driver)
{
  p_driver->args      = NULL;
  p_driver->open      = uartOpenCmd;
  p_driver->close     = uartCloseCmd;
  p_driver->available = uartAvailableCmd;
  p_driver->flush     = uartFlushCmd;
  p_driver->read      = uartReadCmd;
  p_driver->write     = uartWriteCmd;
  return true;
}
