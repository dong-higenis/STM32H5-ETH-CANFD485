/*
 * wdg.h  -  하드웨어 워치독 (IWDG)
 *
 *  정해진 시간 안에 wdgFeed() 로 갱신(refresh)하지 않으면 칩이 스스로 리셋한다.
 *  한 번 켜면 끌 수 없다 (리셋해야 꺼진다).
 */
#ifndef COMMON_HW_WDG_H_
#define COMMON_HW_WDG_H_

#include "hw_def.h"

#ifdef _USE_HW_WDG


bool wdgBegin(uint32_t timeout_ms);   // 켜기 (최대 약 32초)
void wdgFeed(void);                   // 워치독 갱신 (refresh)
bool wdgIsRunning(void);


#endif

#endif
