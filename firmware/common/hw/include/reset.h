#ifndef COMMON_HW_RESET_H_
#define COMMON_HW_RESET_H_

#include "hw_def.h"

#ifdef _USE_HW_RESET

// 직전 리셋의 원인 (여러 개가 동시에 켜질 수 있다)
#define RESET_BIT_PIN     0   // NRST 핀 (리셋 버튼, 디버거)
#define RESET_BIT_POWER   1   // 전원 인가 / 브라운아웃
#define RESET_BIT_SOFT    2   // NVIC_SystemReset()
#define RESET_BIT_IWDG    3   // 독립 워치독
#define RESET_BIT_WWDG    4   // 윈도우 워치독
#define RESET_BIT_LPWR    5   // 저전력 모드 관련
#define RESET_BIT_MAX     6

bool        resetInit(void);
uint32_t    resetGetBits(void);
const char *resetGetBitName(uint8_t bit);
void        resetToReset(void);

#endif

#endif
