#ifndef AP_CMD_CMD_UART_H_
#define AP_CMD_CMD_UART_H_

#include "hw.h"


bool cmdUartInit(void);                             // CLI 에 RX hook 등록 (태스크 시작 전에 한 번)
bool cmdUartInitDriver(cmd_driver_t *p_driver);     // net 태스크가 쓰는 COM 포트용 cmd 드라이버
bool cmdUartTakeHumanEnter(void);                   // CLI 에서 Enter 가 들어왔나? (읽으면 지워짐)


#endif
