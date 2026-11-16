#include "hw.h"


extern uint32_t _fw_flash_begin;


// 이미지 +0x400 (= 0x08010800) 에 고정되는 앱 버전 정보.
// PC 툴은 다운로드 전에 .bin 파일의 이 위치를 읽어 이름을 확인한다.
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

  resetInit();      // 리셋 원인은 부트로더가 이미 읽고 지웠다. 여기서는 resetToReset() 을 쓰려고 준비만 한다
  ledInit();
  uartInit();
  cliInit();
  canInit();        // CAN 은 PC 가 "열어" 줄 때까지 닫혀 있다 (트랜시버 대기)
  logInit();
  logOpen(HW_LOG_CH, HW_LOG_BAUD);

  logPrintf("\r\n[ Firmware Begin... ]\r\n");
  logPrintf("Booting..Name \t\t: %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Booting..Ver  \t\t: %s\r\n", _DEF_FIRMWARE_VERSION);
  logPrintf("Booting..Clock\t\t: %d Mhz\r\n", (int)(HAL_RCC_GetSysClockFreq() / 1000000));
  logPrintf("Booting..Addr \t\t: 0x%08X\r\n", (unsigned)firm_ver.firm_addr);
  logPrintf("Booting..VTOR \t\t: 0x%08X\r\n", (unsigned)SCB->VTOR);
  logPrintf("\r\n");

  return true;
}
