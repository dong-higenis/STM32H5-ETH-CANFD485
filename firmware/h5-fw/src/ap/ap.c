#include "ap.h"
#include "task/led_task.h"
#include "task/cli_task.h"
#include "task/net_task.h"
#include "task/wdg_task.h"


void apInit(void)
{
  ledTaskInit();
  cliTaskInit();
  netTaskInit();
  wdgTaskInit();      
}

void apMain(void)
{
  vTaskStartScheduler();

  // 여기에 왔다면 힙이 모자라 IDLE 태스크조차 못 만든 것
  Error_Handler();
}
