/*
 * wiznet.c  -  네트워크 관리
 *
 *  하는 일
 *    1) W6300 초기화: 리셋 -> 칩 ID 확인 -> 소켓 메모리 나누기 -> MAC 주소 넣기
 *    2) 랜선 감시: 250ms 마다 "연결됐나?" 확인
 *    3) IP 얻기:   DHCP 모드면 공유기에게 IP 를 받아오고, 고정 IP 모드면 정해진 IP 를 쓴다
 *
 *  W6300 을 만지는 코드는 전부 net 태스크 안에서만 돈다. (wiznet.h 의 규칙)
 */
#include "wiznet.h"

#ifdef _USE_HW_WIZNET

#include "wiz_qspi.h"
#include "log.h"
#include "wiznet/wizchip_conf.h"
#include "wiznet/socket/socket.h"
#include "wiznet/dhcp/dhcp.h"


#define SOCKET_DHCP           HW_WIZNET_SOCKET_DHCP
#define W6300_CIDR_VALUE      0x61          // W6300 의 칩 ID 레지스터 값

#define LINK_CHECK_MS         250
#define DHCP_TICK_MS          1000


typedef enum
{
  REQ_NONE = 0,
  REQ_DHCP,
  REQ_STATIC,
} wiznet_req_t;


static wiznet_info_t info;                  // 다른 태스크와 나눠 보는 상태 (복사할 때 잠금)
static volatile wiznet_req_t req = REQ_NONE;
static uint8_t req_ip[4], req_sn[4], req_gw[4];

static uint8_t  dhcp_buf[1024];             // DHCP 메시지 버퍼 (최대 548바이트)
static bool     dhcp_running  = false;
static bool     dhcp_reported = false;
static uint32_t pre_time_link = 0;
static uint32_t pre_time_dhcp = 0;

// 소켓 8개에 W6300 내부 메모리(송신 32KB, 수신 32KB)를 4KB 씩 똑같이 나눠 준다
static uint8_t socket_mem[2][8] =
{
  {4, 4, 4, 4, 4, 4, 4, 4},   // TX
  {4, 4, 4, 4, 4, 4, 4, 4},   // RX
};


static void makeMac(uint8_t mac[6]);
static void setNetRegs(const uint8_t ip[4], const uint8_t sn[4], const uint8_t gw[4]);
static void dhcpStart(void);
static void dhcpStop(void);
static void dhcpAssign(void);
static void dhcpConflict(void);
static void updateLink(void);
static void updateDhcp(void);
static void updateRequest(void);
static void printIp(const char *title);


bool wiznetInit(void)
{
  uint8_t cidr;
  uint8_t lock = SYS_NET_LOCK;
  uint8_t mac_read[6];
  uint8_t zero[4] = {0, 0, 0, 0};

  memset(&info, 0, sizeof(info));
  info.is_dhcp = true;              // 기본은 DHCP

  // 1) QSPI 준비 + 칩 리셋
  if (wizQspiInit() != true)
  {
    logPrintf("[NG] wiznetInit() : QSPI init\r\n");
    return false;
  }
  wizQspiReset();

  // 2) 칩 ID 확인 - 여기서 0x61 이 나오면 QSPI "읽기"가 된다는 뜻
  cidr = WIZCHIP_READ(_CIDR_);
  if (cidr != W6300_CIDR_VALUE)
  {
    logPrintf("[NG] wiznetInit() : chip not found (CIDR 0x%02X, QSPI err %d)\r\n",
              cidr, (int)wizQspiGetErrorCount());
    return false;
  }

  // 3) 소켓 메모리 나누기
  if (ctlwizchip(CW_INIT_WIZCHIP, (void *)socket_mem) != 0)
  {
    logPrintf("[NG] wiznetInit() : socket memory\r\n");
    return false;
  }

  // 4) 네트워크 레지스터 잠금 해제 (W6300 은 실수로 IP 를 바꾸지 않게 잠겨 있다)
  ctlwizchip(CW_SYS_UNLOCK, &lock);

  // 5) MAC 주소 쓰고 다시 읽어 비교 - 같으면 QSPI "쓰기"도 된다는 뜻
  makeMac(info.mac);
  setSHAR(info.mac);
  getSHAR(mac_read);
  if (memcmp(info.mac, mac_read, 6) != 0)
  {
    logPrintf("[NG] wiznetInit() : QSPI write check failed\r\n");
    return false;
  }

  setNetRegs(zero, zero, zero);
  info.is_chip = true;

  logPrintf("[OK] wiznetInit()\r\n");
  logPrintf("     chip : W6300 (CIDR 0x%02X)\r\n", cidr);
  logPrintf("     mac  : %02X:%02X:%02X:%02X:%02X:%02X\r\n",
            info.mac[0], info.mac[1], info.mac[2], info.mac[3], info.mac[4], info.mac[5]);
  return true;
}

void wiznetUpdate(void)
{
  if (info.is_chip != true)
    return;

  updateRequest();
  updateLink();
  updateDhcp();

  info.qspi_error = wizQspiGetErrorCount();
}

void wiznetGetInfo(wiznet_info_t *p_info)
{
  taskENTER_CRITICAL();         // 복사하는 도중에 net 태스크가 값을 바꾸지 못하게
  *p_info = info;
  taskEXIT_CRITICAL();
}

bool wiznetHasIp(void)
{
  return info.is_ip;
}

void wiznetRequestDHCP(void)
{
  req = REQ_DHCP;
}

void wiznetRequestStatic(const uint8_t ip[4], const uint8_t sn[4], const uint8_t gw[4])
{
  memcpy(req_ip, ip, 4);
  memcpy(req_sn, sn, 4);
  memcpy(req_gw, gw, 4);
  __DMB();                // 주소를 다 써 놓은 "다음에" 요청 플래그를 올린다 (순서 보장)
  req = REQ_STATIC;
}


// 다른 태스크가 남긴 요청 처리
static void updateRequest(void)
{
  wiznet_req_t cur = req;

  if (cur == REQ_NONE)
    return;
  req = REQ_NONE;

  if (cur == REQ_DHCP)
  {
    info.is_dhcp = true;
    logPrintf("eth : DHCP mode\r\n");
    if (info.is_link)
      dhcpStart();
  }
  else if (cur == REQ_STATIC)
  {
    dhcpStop();
    info.is_dhcp = false;
    setNetRegs(req_ip, req_sn, req_gw);
    info.is_ip = true;
    printIp("eth : static IP");
  }
}

// 250ms 마다 랜선 상태 확인
static void updateLink(void)
{
  bool link;
  uint8_t zero[4] = {0, 0, 0, 0};

  if (millis() - pre_time_link < LINK_CHECK_MS)
    return;
  pre_time_link = millis();

  link = (wizphy_getphylink() == PHY_LINK_ON);
  if (link == info.is_link)
    return;

  info.is_link = link;
  logPrintf("eth link : %s\r\n", link ? "UP" : "DOWN");

  if (info.is_dhcp)
  {
    if (link)
    {
      dhcpStart();
    }
    else
    {
      // 선이 빠지면 받았던 IP 는 버린다. 다시 꽂으면 새로 받는다.
      dhcpStop();
      setNetRegs(zero, zero, zero);
      info.is_ip = false;
    }
  }
}

// DHCP 진행
static void updateDhcp(void)
{
  uint8_t ret;

  if (dhcp_running != true)
    return;

  // DHCP 라이브러리는 "1초가 지났다"는 신호를 받아서 시간 초과를 잰다
  if (millis() - pre_time_dhcp >= DHCP_TICK_MS)
  {
    pre_time_dhcp = millis();
    DHCP_time_handler();
  }

  ret = DHCP_run();

  if (ret == DHCP_IP_LEASED && dhcp_reported != true)
  {
    dhcp_reported = true;
    printIp("eth dhcp : OK");
  }
  else if (ret == DHCP_FAILED)
  {
    // 라이브러리가 알아서 처음부터 다시 시도한다. 우리는 알리기만 한다.
    logPrintf("eth dhcp : no answer from DHCP server, retrying...\r\n");
  }
}

static void dhcpStart(void)
{
  uint8_t zero[4] = {0, 0, 0, 0};

  setNetRegs(zero, zero, zero);
  info.is_ip = false;

  DHCP_init(SOCKET_DHCP, dhcp_buf);
  reg_dhcp_cbfunc(dhcpAssign, dhcpAssign, dhcpConflict);

  dhcp_running  = true;
  dhcp_reported = false;
  pre_time_dhcp = millis();
  logPrintf("eth dhcp : requesting IP...\r\n");
}

static void dhcpStop(void)
{
  if (dhcp_running)
  {
    DHCP_stop();
    dhcp_running = false;
  }
}

// DHCP 라이브러리가 "IP 받았다" 할 때 부르는 함수
static void dhcpAssign(void)
{
  uint8_t ip[4], sn[4], gw[4];
  uint8_t lock = SYS_NET_LOCK;

  getIPfromDHCP(ip);
  getSNfromDHCP(sn);
  getGWfromDHCP(gw);

  ctlwizchip(CW_SYS_UNLOCK, &lock);     // IP 가 바뀌며 칩이 리셋됐을 수 있으니 다시 잠금 해제
  setSHAR(info.mac);
  setNetRegs(ip, sn, gw);

  taskENTER_CRITICAL();
  getDNSfromDHCP(info.dns);
  info.lease_time = getDHCPLeasetime();
  info.is_ip      = true;
  taskEXIT_CRITICAL();

  dhcp_reported = false;                // 새 IP 를 받았으니 다시 알린다
}

static void dhcpConflict(void)
{
  logPrintf("eth dhcp : IP conflict, retrying...\r\n");
}

static void setNetRegs(const uint8_t ip[4], const uint8_t sn[4], const uint8_t gw[4])
{
  setSIPR((uint8_t *)ip);
  setSUBR((uint8_t *)sn);
  setGAR((uint8_t *)gw);

  taskENTER_CRITICAL();
  memcpy(info.ip, ip, 4);
  memcpy(info.sn, sn, 4);
  memcpy(info.gw, gw, 4);
  taskEXIT_CRITICAL();
}

/*
 * MAC 주소 만들기
 *   보드마다 달라야 한다. 칩 고유번호(UID 96비트)를 섞어서 5바이트를 만든다.
 *   첫 바이트 0x02 = "개인이 정한 주소(공식 등록 X), 한 장비용(unicast)" 표시.
 */
static void makeMac(uint8_t mac[6])
{
  uint32_t uid[3];
  uint32_t hash = 2166136261u;      // FNV-1a 해시 시작값
  uint8_t *p = (uint8_t *)uid;
  uint8_t  x = 0;

  bspGetUID(uid);
  for (int i = 0; i < 12; i++)
  {
    hash = (hash ^ p[i]) * 16777619u;
    x ^= p[i];
  }

  mac[0] = 0x02;
  mac[1] = (uint8_t)(hash >> 24);
  mac[2] = (uint8_t)(hash >> 16);
  mac[3] = (uint8_t)(hash >> 8);
  mac[4] = (uint8_t)(hash >> 0);
  mac[5] = x;
}

static void printIp(const char *title)
{
  logPrintf("%s\r\n", title);
  logPrintf("     ip   : %d.%d.%d.%d\r\n", info.ip[0], info.ip[1], info.ip[2], info.ip[3]);
  logPrintf("     mask : %d.%d.%d.%d\r\n", info.sn[0], info.sn[1], info.sn[2], info.sn[3]);
  logPrintf("     gw   : %d.%d.%d.%d\r\n", info.gw[0], info.gw[1], info.gw[2], info.gw[3]);
}

#endif
