#include "hw.h"


// 이 이미지의 시작 주소
extern uint32_t _fw_flash_begin;

/**
 * @brief 이미지의 +0x400 위치에 고정되는 버전 data.
 *   링커 스크립트의 .version 섹션 -> VER 메모리 영역(0x08000400)에 배치된다.
 *   robot_can-gui tool과 이 주소를 직접 공유하며 부트로더의 버전을 확인한다.
 */
__attribute__((section(".version"), used))
const firm_ver_t firm_ver =
{
  .magic_number = VERSION_MAGIC_NUMBER,
  .version_str  = _DEF_FIRMWARE_VERSION,
  .name_str     = _DEF_BOARD_NAME,
  .firm_addr    = (uint32_t)&_fw_flash_begin,
};


bool hwInit(void)
{ 
  bspInit();
 
  // 리셋 원인은 바로 읽어준다.
  resetInit();

  ledInit();
  uartInit();
  logInit();
  logOpen(HW_LOG_CH, HW_LOG_BAUD);

  logPrintf("\r\n[ Bootloader Begin... ]\r\n");
  logPrintf("Booting..Name \t\t: %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Booting..Ver  \t\t: %s\r\n", _DEF_FIRMWARE_VERSION);
  logPrintf("Booting..Clock\t\t: %d Mhz\r\n", (int)(HAL_RCC_GetSysClockFreq() / 1000000));
  logPrintf("Booting..Addr \t\t: 0x%08X\r\n", (unsigned)firm_ver.firm_addr);

  logPrintf("Reset..Cause  \t\t:");
  for (int i = 0; i < RESET_BIT_MAX; i++)
  {
    if (resetGetBits() & (1 << i))
    {
      logPrintf(" %s", resetGetBitName(i));
    }
  }
  logPrintf("\r\n\r\n");

  return true;
}
