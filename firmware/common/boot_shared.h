/*
 * boot_shared.h  -  부트로더와 앱이 리셋 전후로 주고받는 공유 데이터 (NOINIT RAM)
 *
 *  RAM 맨 앞(0x20000000, NOINIT 영역)은 리셋해도 지워지지 않는다.
 *  (보드 옵션 SRAM1_3_RST = "리셋 때 안 지움", CubeProgrammer 로 확인)
 *  그래서 앱이 여기에 "업데이트 해줘"라고 적고 리셋하면, 부트로더가 읽을 수 있다.
 *
 *  전원을 껐다 켜면 RAM 이 아무 값이나 되므로, magic 으로 유효한 데이터인지 확인한다.
 *
 *  G4 보드는 같은 일을 RTC 백업 레지스터로 했다. 원리는 같다.
 */
#ifndef COMMON_BOOT_SHARED_H_
#define COMMON_BOOT_SHARED_H_

#include <stdint.h>
#include "mem_map.h"


#define BOOT_SHARED_MAGIC       0x424F4F54    // "BOOT"

#define BOOT_REQ_UPDATE         (1 << 0)     
#define BOOT_REQ_STAY           (1 << 1)      

/**
 * @brief 고장 state에 대한 상수
 */
#define FAULT_MAGIC_NEW         0x464C544E   
#define FAULT_MAGIC_SEEN        0x464C5453    
#define FAULT_COUNT_MAX         3           
#define FAULT_HEALTHY_MS        30000         

enum
{
  FAULT_NONE = 0,
  FAULT_HARD,               // HardFault      : 잘못된 주소 읽기/쓰기, 잘못된 명령 등
  FAULT_MEM,                // MemManage
  FAULT_BUS,                // BusFault
  FAULT_USAGE,              // UsageFault     : 0 으로 나누기, 태스크 스택 넘침(STKOF) 등
  FAULT_NMI,                // NMI            : Flash ECC 2비트 오류 등
  FAULT_ERROR,              // Error_Handler  : RTOS 스택 넘침/힙 부족, 초기화 실패
  FAULT_ASSERT,             // configASSERT   : FreeRTOS 가 "말이 안 되는 상황"을 발견
  FAULT_STALL,              // wdg 태스크   : 태스크 하나가 멈춤 (fault_task 가 원인 태스크)
  FAULT_IWDG,               // 하드웨어 워치독 : wdg 태스크까지 멈춤 (부트로더가 리셋 원인으로 앎)
  FAULT_TYPE_MAX
};


typedef struct
{
  uint32_t magic;           // BOOT_SHARED_MAGIC 이면 아래 내용이 유효
  uint32_t request;         // BOOT_REQ_xxx
  uint32_t boot_count;      // 부트로더가 앱으로 넘겨준 횟수 (전원 켠 뒤부터)

  // fault state에 대한 상수
  uint32_t fault_magic;
  uint32_t fault_count;     // 연속 fault 횟수
  uint32_t fault_pc;
  uint32_t fault_lr;
  uint32_t fault_cfsr;
  uint32_t fault_bfar;
  uint32_t fault_task[4];   // 태스크 이름 앞 16 글자

  uint32_t fault_type;      // FAULT_xxx
  uint32_t fault_uptime;    // 앱이 켜진 뒤 몇 ms 만에 죽었나
} boot_shared_t;


#define boot_shared   (*(volatile boot_shared_t *)RAM_ADDR_NOINIT)


static inline void bootSharedCheck(void)
{
  if (boot_shared.magic != BOOT_SHARED_MAGIC)
  {
    // 전원을 막 켰다 -> 쓰레기 값. 깨끗이 시작
    uint32_t *p = (uint32_t *)RAM_ADDR_NOINIT;
    for (uint32_t i = 0; i < sizeof(boot_shared_t) / 4; i++)
      p[i] = 0;
    boot_shared.magic = BOOT_SHARED_MAGIC;
  }
}


static inline const char *faultTypeName(uint32_t type)
{
  static const char *name[FAULT_TYPE_MAX] =
  {
    "none", "HardFault", "MemManage", "BusFault", "UsageFault", "NMI",
    "Error_Handler", "assert", "task stall", "watchdog (IWDG)",
  };
  return (type < FAULT_TYPE_MAX) ? name[type] : "?";
}


#endif
