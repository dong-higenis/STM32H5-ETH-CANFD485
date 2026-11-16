#ifndef AP_TASK_WDG_TASK_H_
#define AP_TASK_WDG_TASK_H_

#include "hw.h"


bool    wdgTaskInit(void);

// 감시받을 태스크 등록 / alive 체크
//   id = aliveAdd("net", 3000);   // 3초 넘게 aliveKick 이 없으면 "멈춤"
//   aliveKick(id);                // 루프마다
int8_t  aliveAdd(const char *name, uint32_t limit_ms);
void    aliveKick(int8_t id);


#endif
