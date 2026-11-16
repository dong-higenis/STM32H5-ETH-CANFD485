#ifndef COMMON_DEF_H_
#define COMMON_DEF_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "err_code.h"
#include "mem_map.h"
#include "image.h"
#include "net_proto.h"


#define _DEF_LED1         0
#define _DEF_LED2         1
#define _DEF_LED3         2
#define _DEF_LED4         3

#define _DEF_UART1        0
#define _DEF_UART2        1
#define _DEF_UART3        2
#define _DEF_UART4        3

#define _DEF_INPUT        0
#define _DEF_OUTPUT       1

#define _DEF_LOW          0
#define _DEF_HIGH         1


#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

#ifndef cmin
#define cmin(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef cmax
#define cmax(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define ARRAY_COUNT(x)    (sizeof(x) / sizeof((x)[0]))


#endif
