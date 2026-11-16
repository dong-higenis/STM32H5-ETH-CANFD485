#ifndef AP_CMD_CMD_CAN_H_
#define AP_CMD_CMD_CAN_H_

#include "hw.h"


void cmdCanInit(void);                // CLI "can" 등록 (RTOS 시작 전에)
bool cmdCanProcess(cmd_t *p_cmd);     // CAN 명령/데이터 패킷 처리 (내 것이 아니면 false)
void cmdCanUpdate(cmd_t *p_cmd);      // CAN 에서 받은 프레임을 PC 로 보내기
void cmdCanClose(void);               // PC 연결이 끊기면 CAN 닫기


#endif
