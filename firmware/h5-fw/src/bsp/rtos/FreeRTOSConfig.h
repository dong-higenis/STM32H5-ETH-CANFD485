/*
 * FreeRTOSConfig.h
 *
 *  FreeRTOS 설정. 처음에는 "태스크 + 시간 지연 + 뮤텍스"만 쓰도록 최소로 켰다.
 *  필요한 기능은 단계가 진행되면서 하나씩 켠다.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)
  #include <stdint.h>
  extern uint32_t SystemCoreClock;
#endif


/* ---- Cortex-M33 포트 설정 (TrustZone 안 씀, FPU 씀) ---- */
#define configENABLE_TRUSTZONE                  0
#define configRUN_FREERTOS_SECURE_ONLY          0
#define configENABLE_FPU                        1
#define configENABLE_MPU                        0


/* ---- 기본 ---- */
#define configUSE_PREEMPTION                    1             // 우선순위 높은 태스크가 끼어들 수 있다
#define configCPU_CLOCK_HZ                      (SystemCoreClock)
#define configTICK_RATE_HZ                      ((TickType_t)1000)   // 1 tick = 1ms
#define configMAX_PRIORITIES                    (8)
#define configMINIMAL_STACK_SIZE                ((uint16_t)256)      // 단위: word(4바이트)
#define configMAX_TASK_NAME_LEN                 (16)
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0


/* ---- 메모리: heap_4, 64KB ---- */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0
#define configTOTAL_HEAP_SIZE                   ((size_t)(64 * 1024))


/* ---- 쓰는 기능 ---- */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_TIMERS                        0
#define configUSE_CO_ROUTINES                   0


/* ---- 디버깅 도움 ---- */
#define configCHECK_FOR_STACK_OVERFLOW          2             // 태스크 스택 넘침 검사 -> vApplicationStackOverflowHook
#define configUSE_MALLOC_FAILED_HOOK            1             // 힙 부족 -> vApplicationMallocFailedHook
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_TRACE_FACILITY                1             // CLI "task" 명령에서 태스크 목록을 보기 위해
#define configUSE_STATS_FORMATTING_FUNCTIONS    0


/* ---- 쓸 수 있게 할 API ---- */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1


/* ---- 인터럽트 우선순위 ----
 *  STM32H5 는 우선순위 4비트(0~15). 숫자가 작을수록 높다.
 *
 *    0 ~ 4  : FreeRTOS 가 절대 막지 않는다. 대신 이 인터럽트 안에서 FreeRTOS 함수를 부르면 안 된다.
 *    5 ~ 15 : FreeRTOS 가 잠깐 막을 수 있다. 이 안에서는 xxxFromISR() 함수를 불러도 된다.
 */
#define configPRIO_BITS                         4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY         (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))


/* ---- 이상한 상황이면 기록하고 리셋한다 (Step 11, hw/driver/fault.c) ----
 *   기록: 종류 assert, PC = 부른 곳, LR 필드 = 그 파일의 줄 번호 */
#if !defined(__ASSEMBLER__)
#include <stdint.h>
extern void faultAssert(uint32_t line);
#endif
#define configASSERT(x)  if ((x) == 0) { faultAssert(__LINE__); }


/* SysTick_Handler / PendSV_Handler / SVC_Handler 는 FreeRTOS 포트(port.c, portasm.c)가 직접 정의한다.
 * 그래서 stm32h5xx_it.c 에는 이 세 함수를 두지 않는다. */

#endif
