#include "qbuffer.h"
#include <string.h>


bool qbufferCreate(qbuffer_t *p_node, uint8_t *p_buf, uint32_t length)
{
  return qbufferCreateBySize(p_node, p_buf, 1, length);
}

bool qbufferCreateBySize(qbuffer_t *p_node, uint8_t *p_buf, uint32_t size, uint32_t length)
{
  if (p_node == NULL || p_buf == NULL || size == 0 || length < 2)
    return false;

  p_node->in    = 0;
  p_node->out   = 0;
  p_node->len   = length;
  p_node->size  = size;
  p_node->p_buf = p_buf;
  return true;
}

bool qbufferWrite(qbuffer_t *p_node, const uint8_t *p_data, uint32_t length)
{
  for (uint32_t i = 0; i < length; i++)
  {
    uint32_t next_in = (p_node->in + 1) % p_node->len;

    if (next_in == p_node->out)   // 가득 참: 새 데이터를 버린다
      return false;

    if (p_data != NULL)
    {
      memcpy(&p_node->p_buf[p_node->in * p_node->size], &p_data[i * p_node->size], p_node->size);
    }
    p_node->in = next_in;
  }
  return true;
}

bool qbufferRead(qbuffer_t *p_node, uint8_t *p_data, uint32_t length)
{
  for (uint32_t i = 0; i < length; i++)
  {
    if (p_node->out == p_node->in)  // 비어 있음
      return false;

    if (p_data != NULL)
    {
      memcpy(&p_data[i * p_node->size], &p_node->p_buf[p_node->out * p_node->size], p_node->size);
    }
    p_node->out = (p_node->out + 1) % p_node->len;
  }
  return true;
}

uint32_t qbufferAvailable(qbuffer_t *p_node)
{
  uint32_t in  = p_node->in;
  uint32_t out = p_node->out;

  return (in + p_node->len - out) % p_node->len;
}

void qbufferFlush(qbuffer_t *p_node)
{
  p_node->in  = 0;
  p_node->out = 0;
}
