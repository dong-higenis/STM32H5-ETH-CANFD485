/*
 * wiz_qspi.h
 *
 *  W6300 과 MCU 사이의 "전화선" (QSPI).
 *  W6300 안의 레지스터/버퍼를 주소로 읽고 쓰는 일만 한다. 네트워크는 모른다.
 *
 *  주소 = (block, addr)
 *    block : 어느 구역인가  0 = 공통 레지스터, 1+4n = 소켓n 레지스터, 2+4n = 소켓n 송신버퍼, 3+4n = 소켓n 수신버퍼
 *    addr  : 그 구역 안의 위치 (0x0000 ~ 0xFFFF)
 */
#ifndef COMMON_HW_WIZ_QSPI_H_
#define COMMON_HW_WIZ_QSPI_H_

#include "hw_def.h"

#ifdef _USE_HW_WIZ_QSPI

bool     wizQspiInit(void);
void     wizQspiReset(void);                  // RST 핀으로 W6300 재시작
bool     wizQspiRead(uint8_t block, uint16_t addr, uint8_t *p_data, uint32_t length);
bool     wizQspiWrite(uint8_t block, uint16_t addr, const uint8_t *p_data, uint32_t length);
uint32_t wizQspiGetErrorCount(void);
bool     wizQspiIntIsActive(void);            // INT 핀 (LOW = W6300 이 할 말 있음)

#endif

#endif
