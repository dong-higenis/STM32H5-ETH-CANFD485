/*
 * net_proto.h  -  랜으로 PC 와 주고받는 프로토콜 정의 (포트 번호, 보드 찾기)
 *
 *  PC 프로그램(software/fdcan-gui/lib/net_find.py)과 숫자, 구조가 같아야 한다.
 *
 *  보드 찾기 (UDP 5001번)
 *    PC  -> 랜 전체(브로드캐스트) : Baram 패킷 [CMD, NET_CMD_FIND, 내용 없음]
 *    보드 -> PC                   : Baram 패킷 [RESP, NET_CMD_FIND, net_find_t]
 *
 *  명령 주고받기 (TCP 5000번) : Step 5 의 Baram 패킷
 */
#ifndef COMMON_NET_PROTO_H_
#define COMMON_NET_PROTO_H_

#include <stdint.h>


#define NET_TCP_CMD_PORT        5000      // 명령 포트 (TCP)
#define NET_UDP_FIND_PORT       5001      // 보드 찾기 포트 (UDP)

#define NET_CMD_FIND            0x0020    // 보드 찾기 요청
#define NET_FIND_MAGIC          0x4E444946  // "FIDN" - 우리 보드의 응답이 맞는지 표시


typedef struct __attribute__((packed))
{
  uint32_t magic;           // NET_FIND_MAGIC
  char     name[32];        // 앱 이름   (firm_ver.name_str)
  char     version[32];     // 앱 버전   (firm_ver.version_str)
  uint8_t  mac[6];
  uint8_t  ip[4];
  uint8_t  sn[4];           // subnet mask
  uint8_t  gw[4];
  uint16_t tcp_port;        // 명령 포트 번호 (5000)
  uint8_t  is_dhcp;         // 1 = DHCP, 0 = 고정 IP
  uint8_t  is_busy;         // 1 = 다른 PC 가 이미 접속 중
} net_find_t;               // 90 바이트

// 크기가 바뀌면 PC 쪽이 잘못 읽는다. 바뀌면 컴파일에서 바로 알려준다.
_Static_assert(sizeof(net_find_t) == 90, "net_find_t must be 90 bytes (PC tool depends on it)");


#endif
