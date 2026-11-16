/*
 * net_find.h  -  "보드 찾기" 응답 (UDP)
 *
 *  netFindUpdate() 는 W6300 을 만지므로 net 태스크만 부른다.
 */
#ifndef COMMON_HW_NET_FIND_H_
#define COMMON_HW_NET_FIND_H_

#include "hw_def.h"

#ifdef _USE_HW_NET_FIND

bool netFindInit(uint8_t socket, uint16_t port);
void netFindUpdate(void);
void netFindMakeReply(net_find_t *p_reply);       // 응답 내용 만들기 (시험하기 쉽게 따로 뺐다)
uint32_t netFindGetAnswerCount(void);
uint32_t netFindGetDropCount(void);

#endif

#endif
