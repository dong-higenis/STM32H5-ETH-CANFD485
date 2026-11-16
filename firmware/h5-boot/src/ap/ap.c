#include "ap.h"
#include "boot/boot.h"
#include "boot_shared.h"


static const char *bootErrStr(uint16_t err_code);
static void        bootFaultCheck(void);


void apInit(void)
{
  uint16_t err_code;
  uint32_t request;

  // 앱이 남긴 boot_shared 요청 읽기 (읽은 뒤에는 지운다: 다음 리셋에서 또 하지 않도록)
  bootSharedCheck();
  request = boot_shared.request;
  boot_shared.request = 0;

  bootFaultCheck();

  if (request & BOOT_REQ_UPDATE)
  {
    logPrintf("[  ] request : update\r\n");
    err_code = bootUpdateFirm();
    if (err_code != OK)
      logPrintf("[NG] update : %s (0x%04X)\r\n", bootErrStr(err_code), err_code);
  }
  else if (bootVerifyFirm() != OK && bootVerifyUpdate() == OK)
  {
    logPrintf("[  ] firmware broken, restore from update slot\r\n");
    bootUpdateFirm();
  }

  if (request & BOOT_REQ_STAY)
  {
    logPrintf("[  ] request : stay in bootloader\r\n\r\n");
    logPrintf("Boot Mode..\r\n");
    return;
  }

  if (boot_shared.fault_count >= FAULT_COUNT_MAX)
  {
    logPrintf("[NG] firmware faulted %d times in a row -> stay in bootloader\r\n", (int)boot_shared.fault_count);
    logPrintf("     (press reset to try the firmware again)\r\n\r\n");
    boot_shared.fault_count = 0;        // 사용자가 리셋 버튼을 누르면 다시 한 번 시도한다
    logPrintf("Boot Mode..\r\n");
    return;
  }

  logPrintf("[  ] bootVerifyFirm()\r\n");
  boot_shared.boot_count++;
  err_code = bootJumpFirm();      // 성공하면 여기서 돌아오지 않는다

  logPrintf("[NG] firmware : %s (0x%04X)\r\n\r\n", bootErrStr(err_code), err_code);
  logPrintf("Boot Mode..\r\n");
}

void apMain(void)
{
  uint32_t pre_time = millis();

  while (1)
  {
    bootWdgFeed();

    if (millis() - pre_time >= 100)
    {
      pre_time = millis();
      ledToggle(HW_LED_CH_STATUS);
    }

    if (uartAvailable(HW_UART_CH_DEBUG) > 0)
    {
      uint8_t ch = uartRead(HW_UART_CH_DEBUG);

      uartPrintf(HW_UART_CH_DEBUG, "rx : 0x%02X '%c'\r\n", ch, (ch >= 0x20 && ch < 0x7F) ? ch : '.');
    }
  }
}

static void bootFaultCheck(void)
{
  char name[17] = {0};

  // IWDG 리셋: 앱이 기록할 틈 없이 리셋됐다. 여기서 대신 기록한다.
  if (resetGetBits() & (1 << RESET_BIT_IWDG))
  {
    boot_shared.fault_type   = FAULT_IWDG;
    boot_shared.fault_pc     = 0;
    boot_shared.fault_lr     = 0;
    boot_shared.fault_cfsr   = 0;
    boot_shared.fault_bfar   = 0;
    boot_shared.fault_uptime = 0;
    for (int i = 0; i < 4; i++)
      boot_shared.fault_task[i] = 0;
    boot_shared.fault_count++;
    boot_shared.fault_magic  = FAULT_MAGIC_NEW;
  }

  if (boot_shared.fault_magic != FAULT_MAGIC_NEW)
    return;

  memcpy(name, (const void *)boot_shared.fault_task, 16);

  logPrintf("[!!] firmware fault : %s (%d in a row)\r\n",
            faultTypeName(boot_shared.fault_type), (int)boot_shared.fault_count);
  logPrintf("     task %s, uptime %d ms\r\n", name[0] ? name : "-", (int)boot_shared.fault_uptime);
  logPrintf("     pc 0x%08X  lr 0x%08X  cfsr 0x%08X  bfar 0x%08X\r\n",
            (unsigned)boot_shared.fault_pc, (unsigned)boot_shared.fault_lr,
            (unsigned)boot_shared.fault_cfsr, (unsigned)boot_shared.fault_bfar);

  boot_shared.fault_magic = FAULT_MAGIC_SEEN;  
}

static const char *bootErrStr(uint16_t err_code)
{
  switch (err_code)
  {
    case ERR_BOOT_TAG_MAGIC:  return "no TAG (empty slot?)";
    case ERR_BOOT_TAG_SIZE:   return "TAG size/addr out of range";
    case ERR_BOOT_FW_CRC:     return "CRC mismatch";
    case ERR_BOOT_FLASH_READ: return "flash ECC error";
    case ERR_BOOT_INVALID_FW: return "invalid vector table";
    case ERR_BOOT_FLASH_ERASE: return "flash erase failed";
    case ERR_BOOT_FLASH_WRITE: return "flash write failed";
    default:                  return "unknown";
  }
}
