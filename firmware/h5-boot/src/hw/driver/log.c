#include "log.h"
#include "uart.h"

#ifdef _USE_HW_LOG


static bool    is_open = false;
static uint8_t log_ch  = HW_LOG_CH;


bool logInit(void)
{
  is_open = false;
  return true;
}

bool logOpen(uint8_t ch, uint32_t baud)
{
  log_ch  = ch;
  is_open = uartOpen(ch, baud);
  return is_open;
}

void logPrintf(const char *fmt, ...)
{
  char buf[256];
  va_list args;
  int len;

  if (is_open != true) return;

  va_start(args, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (len > 0)
  {
    uartWrite(log_ch, (uint8_t *)buf, cmin((uint32_t)len, sizeof(buf) - 1));
  }
}

#endif
