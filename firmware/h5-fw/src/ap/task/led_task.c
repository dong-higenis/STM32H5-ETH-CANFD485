#include "led_task.h"


static void ledTask(void *arg);


bool ledTaskInit(void)
{
  BaseType_t ret;

  ret = xTaskCreate(ledTask,              // 실행할 함수
                    "led",                // 이름 (CLI 의 task 명령에 보임)
                    256,                  // 스택 크기 (word 단위 = 1KB)
                    NULL,                 // 함수에 넘길 값
                    TASK_PRIO_LOW,        // 우선순위
                    NULL);                // 핸들 (필요 없음)

  return (ret == pdPASS);
}

static void ledTask(void *arg)
{
  (void)arg;

  while (1)
  {
    ledToggle(HW_LED_CH_STATUS);
    delay(1000);                          // RTOS 가 돌고 있으니 vTaskDelay 가 쓰인다
  }
}
