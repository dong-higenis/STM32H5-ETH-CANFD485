#ifndef COMMON_HW_LOG_H_
#define COMMON_HW_LOG_H_

#include "hw_def.h"

#ifdef _USE_HW_LOG

bool logInit(void);
bool logOpen(uint8_t ch, uint32_t baud);
void logPrintf(const char *fmt, ...);

#else

#define logPrintf(...)

#endif

#endif
