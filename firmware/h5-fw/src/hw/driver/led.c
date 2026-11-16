#include "led.h"

#ifdef _USE_HW_LED


typedef struct
{
  GPIO_TypeDef *port;
  uint16_t      pin;
  GPIO_PinState on_state;
  GPIO_PinState off_state;
} led_tbl_t;


// 회로도: PC13_LED 는 LED 의 캐소드 쪽 -> LOW 를 내보내야 켜진다
static const led_tbl_t led_tbl[LED_MAX_CH] =
{
  {GPIOC, GPIO_PIN_13, GPIO_PIN_RESET, GPIO_PIN_SET},
};


bool ledInit(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();

  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;

  for (int i = 0; i < LED_MAX_CH; i++)
  {
    gpio.Pin = led_tbl[i].pin;
    HAL_GPIO_Init(led_tbl[i].port, &gpio);
    ledOff(i);
  }
  return true;
}

void ledOn(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;
  HAL_GPIO_WritePin(led_tbl[ch].port, led_tbl[ch].pin, led_tbl[ch].on_state);
}

void ledOff(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;
  HAL_GPIO_WritePin(led_tbl[ch].port, led_tbl[ch].pin, led_tbl[ch].off_state);
}

void ledToggle(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;
  HAL_GPIO_TogglePin(led_tbl[ch].port, led_tbl[ch].pin);
}

#endif
