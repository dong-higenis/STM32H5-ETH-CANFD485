/*
 * fault.h  -  앱이 죽을 때 "왜, 어디서" 를 기록하고 다시 켜기 (Step 11)
 *
 *  기록은 boot_shared (리셋해도 남는 RAM) 에 한다. 다음 부팅 때 부트로더가 화면에 보여준다.
 */
#ifndef COMMON_HW_FAULT_H_
#define COMMON_HW_FAULT_H_

#include "hw_def.h"
#include "boot_shared.h"

#ifdef _USE_HW_FAULT


// 기록하고 리셋한다. 돌아오지 않는다.
//   type : FAULT_xxx,  pc : 죽은 곳,  lr : 그 함수를 부른 곳 (assert 는 줄 번호)
//   task : 원인 태스크 이름 (NULL 이면 지금 돌던 태스크)
void faultReset(uint32_t type, uint32_t pc, uint32_t lr, const char *task) __attribute__((noreturn));

void faultAssert(uint32_t line) __attribute__((noreturn));   // FreeRTOSConfig.h 의 configASSERT 가 부름


#endif

#endif
