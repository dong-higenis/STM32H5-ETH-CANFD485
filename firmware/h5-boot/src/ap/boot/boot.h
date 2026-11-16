#ifndef AP_BOOT_H_
#define AP_BOOT_H_

#include "hw.h"


uint16_t bootVerifyFirm(void);
uint16_t bootVerifyUpdate(void);
uint16_t bootUpdateFirm(void);       // 업데이트 영역 -> 앱 영역 복사
uint16_t bootJumpFirm(void);


// 앱이 켠 IWDG 가 리셋 뒤에도 돌고 있으면 갱신(refresh)한다 (Step 11 안전장치)
// 보통은 리셋하면 IWDG 도 꺼지지만, 긴 복사 중에 리셋되는 일이 없도록 한 번 더 막아 둔다.
static inline void bootWdgFeed(void)
{
  if (IWDG->SR & IWDG_SR_ONF)
    IWDG->KR = 0xAAAA;
}


#endif
