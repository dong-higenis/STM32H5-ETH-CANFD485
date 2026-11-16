#include "flash.h"

#ifdef _USE_HW_FLASH


#define FLASH_BANK_BYTES    ((FLASH_ADDR_END - FLASH_ADDR_BOOT) / 2)     // 512KB
#define FLASH_BANK2_ADDR    (FLASH_ADDR_BOOT + FLASH_BANK_BYTES)          // 0x08080000


static bool isWritable(uint32_t addr, uint32_t length);
static void cacheInvalidate(void);


bool flashErase(uint32_t addr, uint32_t length)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t               err   = 0;
  uint32_t               start_sec, end_sec;
  bool                   ret   = true;

  if (length == 0 || isWritable(addr, length) != true)
    return false;

  // 지울 범위를 섹터 번호로 (전체 Flash 기준 0~127)
  start_sec = (addr - FLASH_ADDR_BOOT) / FLASH_SECTOR_SIZE;
  end_sec   = (addr + length - 1 - FLASH_ADDR_BOOT) / FLASH_SECTOR_SIZE;

  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

  // 은행별로 나눠서 지운다 (은행마다 섹터 번호가 0 부터)
  for (uint32_t bank = 0; bank < 2 && ret; bank++)
  {
    uint32_t bank_first = bank * (FLASH_BANK_BYTES / FLASH_SECTOR_SIZE);
    uint32_t bank_last  = bank_first + (FLASH_BANK_BYTES / FLASH_SECTOR_SIZE) - 1;
    uint32_t s = cmax(start_sec, bank_first);
    uint32_t e = cmin(end_sec, bank_last);

    if (s > e)
      continue;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Banks     = (bank == 0) ? FLASH_BANK_1 : FLASH_BANK_2;
    erase.Sector    = s - bank_first;
    erase.NbSectors = e - s + 1;
    if (HAL_FLASHEx_Erase(&erase, &err) != HAL_OK)
      ret = false;
  }

  HAL_FLASH_Lock();
  cacheInvalidate();
  return ret;
}

bool flashWrite(uint32_t addr, const uint8_t *p_data, uint32_t length)
{
  uint32_t qw_addr;
  uint32_t end = addr + length;
  bool     ret = true;
  uint32_t qw[4];       // 16바이트 (4바이트 정렬된 RAM 에 둔다)

  if (length == 0 || isWritable(addr, length) != true)
    return false;

  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

  // 16바이트(quad-word) 단위로 하나씩: 우리 데이터가 걸친 부분만 채우고 나머지는 0xFF
  for (qw_addr = addr & ~0x0FU; qw_addr < end && ret; qw_addr += 16)
  {
    uint8_t *p_qw = (uint8_t *)qw;

    memset(qw, 0xFF, sizeof(qw));
    for (uint32_t i = 0; i < 16; i++)
    {
      uint32_t a = qw_addr + i;
      if (a >= addr && a < end)
        p_qw[i] = p_data[a - addr];
    }

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, qw_addr, (uint32_t)qw) != HAL_OK)
      ret = false;
  }

  HAL_FLASH_Lock();
  cacheInvalidate();
  return ret;
}

bool flashRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  if (addr < FLASH_ADDR_BOOT || addr + length > FLASH_ADDR_END)
    return false;
  memcpy(p_data, (const void *)addr, length);
  return true;
}


// 부트로더 영역과 Flash 바깥은 막는다
static bool isWritable(uint32_t addr, uint32_t length)
{
  if (addr < FLASH_ADDR_FIRM)             return false;   // 부트로더 보호
  if (addr + length > FLASH_ADDR_END)     return false;   // 없는 주소
  if (addr + length < addr)               return false;   // 넘침
  return true;
}

static void cacheInvalidate(void)
{
  if (ICACHE->CR & ICACHE_CR_EN)
  {
    ICACHE->CR |= ICACHE_CR_CACHEINV;
    while (ICACHE->SR & ICACHE_SR_BUSYF)
    {
    }
  }
}

#endif
