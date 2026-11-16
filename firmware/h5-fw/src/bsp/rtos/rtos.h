#ifndef BSP_RTOS_H_
#define BSP_RTOS_H_

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"


// 태스크 우선순위 (숫자가 클수록 먼저 실행된다. 0 은 IDLE 태스크)
#define TASK_PRIO_LOW       1
#define TASK_PRIO_NORMAL    2
#define TASK_PRIO_HIGH      3


bool rtosIsRunning(void);


#endif
