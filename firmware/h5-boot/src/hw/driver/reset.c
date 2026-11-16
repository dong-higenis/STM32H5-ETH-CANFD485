/**
 * @brief RCC_RSR 레지스터에 "어떤 사유로 리셋되었는가"가 기록돼 있다.
 * 이 Flag는 직접 수동으로 지우기 전까지 남아있으므로 booting 직후에 읽고 지우면된다.
 */
#include "reset.h"

#ifdef _USE_HW_RESET


static uint32_t reset_bits = 0;

static const char *reset_bit_name[RESET_BIT_MAX] =
{
  "PIN",
  "POWER",
  "SOFT",
  "IWDG",
  "WWDG",
  "LPWR",
};


bool resetInit(void)
{
  reset_bits = 0;

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST))  reset_bits |= (1 << RESET_BIT_PIN);
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST))  reset_bits |= (1 << RESET_BIT_POWER);
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST))  reset_bits |= (1 << RESET_BIT_SOFT);
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) reset_bits |= (1 << RESET_BIT_IWDG);
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST)) reset_bits |= (1 << RESET_BIT_WWDG);
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST)) reset_bits |= (1 << RESET_BIT_LPWR);

  __HAL_RCC_CLEAR_RESET_FLAGS();

  return true;
}

uint32_t resetGetBits(void)
{
  return reset_bits;
}

const char *resetGetBitName(uint8_t bit)
{
  if (bit >= RESET_BIT_MAX) return "";
  return reset_bit_name[bit];
}

void resetToReset(void)
{
  HAL_NVIC_SystemReset();
}

#endif
