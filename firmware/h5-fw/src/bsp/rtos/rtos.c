/**
* @brief 
* 
*  FreeRTOS 에 "문제가 생겼다"고 알려주기 위한 함수들(hook)
*/
#include "bsp.h"
#include "rtos.h"


bool rtosIsRunning(void)
{
  return (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
}

/**
 * @brief 태스크의 스택이 넘친 상황을 알린다. -> xTaskCreate내 인자 스택 크기를 늘리도록 한다.
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  (void)pcTaskName;
  Error_Handler();
}

/**
 * @brief 힙(configTOTAL_HEAP_SIZE)가 모자라 태스크/큐를 만들지 못한상황을 알린다.
 */
void vApplicationMallocFailedHook(void)
{
  Error_Handler();
}
