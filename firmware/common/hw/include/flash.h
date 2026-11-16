/*
 * flash.h  -  내부 Flash 지우기/쓰기/읽기 (부트로더와 앱이 같은 코드를 쓴다)
 *
 *  안전 장치: 부트로더 영역(0x08000000 ~ 0x0800FFFF)는 절대 지우거나 쓰지 않는다.
 *             부트로더가 망가지면 USB DFU 나 ST-Link 로만 살릴 수 있기 때문이다.
 */
#ifndef COMMON_HW_FLASH_H_
#define COMMON_HW_FLASH_H_

#include "hw_def.h"

#ifdef _USE_HW_FLASH

bool flashErase(uint32_t addr, uint32_t length);                   // 8KB 섹터 단위로 지운다
bool flashWrite(uint32_t addr, const uint8_t *p_data, uint32_t length);
bool flashRead(uint32_t addr, uint8_t *p_data, uint32_t length);

#endif

#endif
