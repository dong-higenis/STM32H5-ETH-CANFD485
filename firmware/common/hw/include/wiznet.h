/*
 * wiznet.h  -  네트워크 관리 (W6300 칩 확인, MAC, 링크, DHCP/고정 IP)
 *
 *  규칙: W6300 을 직접 만지는 함수(wiznetInit, wiznetUpdate)는 "net 태스크"만 부른다.
 *        다른 태스크(CLI 등)는 wiznetGetInfo() 로 상태를 복사해 가고,
 *        설정을 바꾸고 싶으면 wiznetRequestXXX() 로 요청만 남긴다.
 */
#ifndef COMMON_HW_WIZNET_H_
#define COMMON_HW_WIZNET_H_

#include "hw_def.h"

#ifdef _USE_HW_WIZNET

typedef struct
{
  bool     is_chip;         // W6300 을 찾았나
  bool     is_link;         // 랜선이 연결됐나 (상대 장비와 신호가 통하나)
  bool     is_dhcp;         // true = DHCP 모드, false = 고정 IP 모드
  bool     is_ip;           // 쓸 수 있는 IP 를 갖고 있나

  uint8_t  mac[6];
  uint8_t  ip[4];
  uint8_t  sn[4];           // subnet mask
  uint8_t  gw[4];           // gateway
  uint8_t  dns[4];
  uint32_t lease_time;      // DHCP 로 받은 IP 의 사용 기한 (초)
  uint32_t qspi_error;
} wiznet_info_t;


// ---- net 태스크 전용 ----
bool wiznetInit(void);
void wiznetUpdate(void);            // 10ms 마다 불러준다

// ---- 아무 태스크 ----
void wiznetGetInfo(wiznet_info_t *p_info);
bool wiznetHasIp(void);             // IP 가 있나 (DHCP 에서 랜선이 빠지면 false)
void wiznetRequestDHCP(void);
void wiznetRequestStatic(const uint8_t ip[4], const uint8_t sn[4], const uint8_t gw[4]);

#endif

#endif
