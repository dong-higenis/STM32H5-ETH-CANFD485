/*
 * tcp_server.h  -  TCP 서버 (PC 의 접속을 기다리는 쪽)
 *
 *  W6300 소켓 하나로 포트 하나를 열고, 한 번에 PC 한 대만 받는다.
 *  (COM 포트를 한 프로그램만 열 수 있는 것과 같다)
 *
 *  tcpServerUpdate() 는 W6300 을 만지므로 net 태스크만 부른다.
 *  Available / Read / Write 도 같은 태스크(net)에서 부른다.
 */
#ifndef COMMON_HW_TCP_SERVER_H_
#define COMMON_HW_TCP_SERVER_H_

#include "hw_def.h"

#ifdef _USE_HW_TCP_SERVER

bool     tcpServerInit(uint8_t socket, uint16_t port);
void     tcpServerUpdate(void);
bool     tcpServerIsConnected(void);
bool     tcpServerIsNewConnect(void);             // 방금 새로 연결됐으면 한 번만 true
uint32_t tcpServerAvailable(void);
uint8_t  tcpServerRead(void);
uint32_t tcpServerWrite(const uint8_t *p_data, uint32_t length);
void     tcpServerFlush(void);
void     tcpServerDrop(void);                      // 지금 클라이언트 연결을 끊는다 (IP 를 잃었을 때)
void     tcpServerSetDebug(bool enable);           // 주고받은 바이트를 로그로 보기
void     tcpServerGetStat(uint32_t *p_rx, uint32_t *p_tx, uint32_t *p_tx_fail);

#endif

#endif
