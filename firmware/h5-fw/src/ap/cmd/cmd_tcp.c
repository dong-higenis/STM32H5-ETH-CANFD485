#include "cmd_tcp.h"


/**
 * @brief cmd에 전달할 함수들을 정의한다.
 */


/**
 * @brief TCP 열기
 *        TCP 서버는 net_task 에서 이미 켜 두므로 할 일이 없다.
 */
static bool tcpOpen(void *args)
{
  (void)args;
  return true;
}

/**
 * @brief TCP 닫기
 *        서버는 계속 켜 두므로 할 일이 없다.
 */
static bool tcpClose(void *args)
{
  (void)args;
  return true;
}

/**
 * @brief 받아 둔 데이터 바이트 수 (0 이면 읽을 것이 없다)
 */
static uint32_t tcpAvailable(void *args)
{
  (void)args;
  return tcpServerAvailable();
}

/**
 * @brief 받아 둔 데이터를 모두 버린다.
 */
static bool tcpFlush(void *args)
{
  (void)args;
  tcpServerFlush();
  return true;
}

/**
 * @brief 받아 둔 데이터에서 1바이트를 꺼낸다.
 *        비어 있으면 0 을 돌려주므로, 먼저 tcpAvailable() 로 확인한다.
 */
static uint8_t tcpRead(void *args)
{
  (void)args;
  return tcpServerRead();
}

/**
 * @brief p_data 에서 length 바이트를 PC 로 보낸다.
 *        보낸 바이트 수를 돌려준다. (PC 가 연결돼 있지 않으면 0)
 */
static uint32_t tcpWrite(void *args, const uint8_t *p_data, uint32_t length)
{
  (void)args;
  return tcpServerWrite(p_data, length);
}


/**
 * @brief cmd 드라이버의 함수 포인터 6개에 TCP 함수들을 연결한다.
 *        net_task 에서 cmdInit() 전에 한 번 부른다.
 */
bool cmdTcpInitDriver(cmd_driver_t *p_driver)
{
  p_driver->args      = NULL;
  p_driver->open      = tcpOpen;
  p_driver->close     = tcpClose;
  p_driver->available = tcpAvailable;
  p_driver->flush     = tcpFlush;
  p_driver->read      = tcpRead;
  p_driver->write     = tcpWrite;
  return true;
}
