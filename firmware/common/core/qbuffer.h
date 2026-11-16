#ifndef COMMON_CORE_QBUFFER_H_
#define COMMON_CORE_QBUFFER_H_

#include <stdint.h>
#include <stdbool.h>


typedef struct
{
  volatile uint32_t in;     
  volatile uint32_t out;    
  uint32_t len;             
  uint32_t size;           
  uint8_t *p_buf;
} qbuffer_t;


bool     qbufferCreate(qbuffer_t *p_node, uint8_t *p_buf, uint32_t length);
bool     qbufferCreateBySize(qbuffer_t *p_node, uint8_t *p_buf, uint32_t size, uint32_t length);
bool     qbufferWrite(qbuffer_t *p_node, const uint8_t *p_data, uint32_t length);
bool     qbufferRead(qbuffer_t *p_node, uint8_t *p_data, uint32_t length);
uint32_t qbufferAvailable(qbuffer_t *p_node);
void     qbufferFlush(qbuffer_t *p_node);


#endif
