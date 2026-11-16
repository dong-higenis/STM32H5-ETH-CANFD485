#ifndef COMMON_CORE_UTIL_H_
#define COMMON_CORE_UTIL_H_

#include <stdint.h>


void     utilUpdateCrc(uint16_t *p_crc_cur, uint8_t data_in);
uint16_t utilCrc16(uint16_t crc, const uint8_t *p_data, uint32_t length);


#endif
