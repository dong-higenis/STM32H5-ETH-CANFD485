#ifndef HW_H_
#define HW_H_

#include "hw_def.h"

#include "led.h"
#include "uart.h"
#include "log.h"
#include "reset.h"
#include "cli.h"
#include "wiz_qspi.h"
#include "wiznet.h"
#include "tcp_server.h"
#include "cmd.h"
#include "net_find.h"
#include "can.h"
#include "flash.h"
#include "fault.h"
#include "wdg.h"


bool hwInit(void);


#endif
