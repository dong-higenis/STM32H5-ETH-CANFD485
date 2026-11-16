#ifndef AP_CMD_CMD_RS485_H_
#define AP_CMD_CMD_RS485_H_

#include "hw.h"


void cmdRs485Init(void);                // CLI "rs485" 등록 (RTOS 시작 전에)
bool cmdRs485Process(cmd_t *p_cmd);     // RS485 명령/데이터 패킷 처리 (내 것이 아니면 false)
void cmdRs485Update(cmd_t *p_cmd);      // RS485 에서 받은 바이트를 PC 로 보내기
void cmdRs485Close(void);               // PC 연결이 끊기면 닫기


#endif
