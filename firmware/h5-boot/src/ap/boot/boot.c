/*
 * boot.c
 *
 *  부트로더의 핵심 두 가지
 *    1) bootVerifyFirm() : Firmware 슬롯의 앱이 온전한지 검사
 *    2) bootJumpFirm()   : 검사를 통과하면 앱으로 실행을 넘김
 *
 *  Firmware 슬롯 (FLASH_ADDR_FIRM = 0x08010000)
 *    +0x000  firm_tag_t   magic / fw_addr / fw_size / fw_crc
 *    +0x400  앱 이미지     [0] 초기 스택 주소  [1] Reset_Handler 주소  ...
 */
#include "boot.h"
#include "util.h"


extern volatile uint32_t bsp_flash_ecc_error;   // stm32h5xx_it.c 의 NMI 처리기가 올린다


static void     bootJumpToApp(uint32_t sp, uint32_t pc) __attribute__((noreturn));
static uint16_t bootVerifySlot(uint32_t slot_addr, uint32_t slot_size);


uint16_t bootVerifyFirm(void)
{
  return bootVerifySlot(FLASH_ADDR_FIRM, FLASH_SIZE_FIRM);
}

uint16_t bootVerifyUpdate(void)
{
  return bootVerifySlot(FLASH_ADDR_UPDATE, FLASH_SIZE_UPDATE);
}

// 슬롯 하나(앱 영역 또는 업데이트 영역)가 온전한지
static uint16_t bootVerifySlot(uint32_t slot_addr, uint32_t slot_size)
{
  const firm_tag_t *p_tag = (const firm_tag_t *)slot_addr;
  const uint8_t    *p_image;
  uint16_t          crc;

  bsp_flash_ecc_error = 0;

  // 1) TAG magic 이 맞는가 - 빈 슬롯(0xFF)이면 여기서 걸린다
  if (p_tag->magic_number != TAG_MAGIC_NUMBER)
    return ERR_BOOT_TAG_MAGIC;

  // 2) 크기와 위치가 슬롯 안에 들어가는가 - 엉뚱한 값으로 슬롯 밖을 읽는 것을 막는다
  if (p_tag->fw_addr != FLASH_SIZE_TAG)
    return ERR_BOOT_TAG_SIZE;
  if (p_tag->fw_size == 0 || p_tag->fw_size > (slot_size - FLASH_SIZE_TAG))
    return ERR_BOOT_TAG_SIZE;

  // 3) 내용 전체의 CRC 가 맞는가
  p_image = (const uint8_t *)(slot_addr + p_tag->fw_addr);
  crc     = utilCrc16(0, p_image, p_tag->fw_size);

  // 읽는 도중 Flash ECC 이중 오류(쓰다 만 영역)가 있었다면 데이터를 믿을 수 없다
  if (bsp_flash_ecc_error != 0)
    return ERR_BOOT_FLASH_READ;

  if ((uint16_t)p_tag->fw_crc != crc)
    return ERR_BOOT_FW_CRC;

  return OK;
}

/*
 * 업데이트 영역(Bank2) -> 앱 영역(Bank1) 복사
 *
 *   1) 업데이트 영역이 온전한지 확인 (망가진 것을 복사하면 안 된다)
 *   2) 앱 영역을 필요한 만큼 지운다
 *   3) 512 바이트씩 복사
 *   4) 앱 영역이 온전한지 다시 확인
 *
 *   도중에 전원이 꺼지면? 앱 영역은 망가지지만 업데이트 영역은 그대로다.
 *   다음 부팅 때 "앱은 망가졌고 업데이트 영역은 멀쩡함" -> 다시 복사한다. (ap.c)
 */
uint16_t bootUpdateFirm(void)
{
  const firm_tag_t *p_tag = (const firm_tag_t *)FLASH_ADDR_UPDATE;
  uint32_t          total;
  uint16_t          err;

  err = bootVerifyUpdate();
  if (err != OK)
    return err;

  total = FLASH_SIZE_TAG + p_tag->fw_size;
  if (total > FLASH_SIZE_FIRM)
    return ERR_BOOT_TAG_SIZE;

  logPrintf("[  ] update : %d bytes, erase...\r\n", (int)total);
  if (flashErase(FLASH_ADDR_FIRM, total) != true)
    return ERR_BOOT_FLASH_ERASE;

  logPrintf("[  ] update : copy...\r\n");
  for (uint32_t i = 0; i < total; i += 512)
  {
    uint32_t len = cmin(total - i, 512U);

    if (flashWrite(FLASH_ADDR_FIRM + i, (const uint8_t *)(FLASH_ADDR_UPDATE + i), len) != true)
      return ERR_BOOT_FLASH_WRITE;
    if ((i & 0x3FFF) == 0)
    {
      ledToggle(HW_LED_CH_STATUS);         // 복사 중 LED 깜빡
      bootWdgFeed();
    }
  }

  err = bootVerifyFirm();
  logPrintf("[%s] update : %s\r\n", err == OK ? "OK" : "NG", err == OK ? "done" : "verify failed");
  return err;
}

uint16_t bootJumpFirm(void)
{
  uint16_t  err_code;
  uint32_t *p_vector = (uint32_t *)(FLASH_ADDR_FIRM + FLASH_SIZE_TAG);
  uint32_t  app_sp;
  uint32_t  app_pc;

  err_code = bootVerifyFirm();
  if (err_code != OK)
    return err_code;

  // Vector table 의 첫 두 항목: [0] 스택 초기값, [1] Reset_Handler 주소
  app_sp = p_vector[0];
  app_pc = p_vector[1];

  // CRC 가 맞아도 링크 주소가 틀린 이미지일 수 있으니 한 번 더 확인한다
  if (app_sp < RAM_ADDR_NOINIT || app_sp > (RAM_ADDR_NOINIT + 640 * 1024))
    return ERR_BOOT_INVALID_FW;
  if (app_pc < FLASH_ADDR_FIRM || app_pc >= (FLASH_ADDR_FIRM + FLASH_SIZE_FIRM))
    return ERR_BOOT_INVALID_FW;
  if ((app_pc & 1) == 0)    // Cortex-M 은 Thumb 모드만 있으므로 주소 최하위 비트가 1 이어야 한다
    return ERR_BOOT_INVALID_FW;

  logPrintf("[  ] bootJumpFirm()\r\n");
  logPrintf("     vector : 0x%08X\r\n", (unsigned)p_vector);
  logPrintf("     sp     : 0x%08X\r\n", (unsigned)app_sp);
  logPrintf("     pc     : 0x%08X\r\n\r\n", (unsigned)app_pc);

  // 부트로더가 켜둔 클럭, 인터럽트, 주변장치를 모두 리셋 상태로 되돌린다
  bspDeInit();

  // 앞으로 인터럽트가 오면 앱의 Vector table 을 보도록 바꾼다
  SCB->VTOR = (uint32_t)p_vector;
  __DSB();
  __ISB();

  bootJumpToApp(app_sp, app_pc);
}

/*
 * 스택 포인터를 바꾸는 순간 C 코드의 지역 변수(스택에 있을 수 있음)는 믿을 수 없게 된다.
 * 그래서 sp, pc 를 레지스터에 담은 채로 어셈블리 몇 줄로 끝낸다.
 */
static void bootJumpToApp(uint32_t sp, uint32_t pc)
{
  __asm volatile(
    "msr  msplim, %2  \n"   // 스택 한계 검사 해제 (Cortex-M33 기능)
    "msr  msp, %0     \n"   // 앱의 초기 스택으로 교체
    "cpsie i          \n"   // bspDeInit 에서 막은 인터럽트 허용 (소스는 모두 꺼진 상태)
    "bx   %1          \n"   // 앱의 Reset_Handler 로 이동
    :
    : "r"(sp), "r"(pc), "r"(0)
    : "memory");

  while (1)
  {
  }
}
