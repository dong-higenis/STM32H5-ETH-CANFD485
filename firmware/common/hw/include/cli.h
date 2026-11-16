/*
 * cli.h
 *
 *  터미널 CLI. Baram 프로젝트의 cli 와 같은 사용법이다.
 *
 *    static void cliHello(cli_args_t *args)
 *    {
 *      if (args->argc == 1 && args->isStr(0, "world"))
 *        cliPrintf("hello world!\n");
 *    }
 *    cliAdd("hello", cliHello);      // 터미널에서 "hello world" 입력
 */
#ifndef COMMON_HW_CLI_H_
#define COMMON_HW_CLI_H_

#include "hw_def.h"

#ifdef _USE_HW_CLI

#define CLI_CMD_LIST_MAX      HW_CLI_CMD_LIST_MAX
#define CLI_CMD_NAME_MAX      HW_CLI_CMD_NAME_MAX
#define CLI_LINE_BUF_MAX      HW_CLI_LINE_BUF_MAX
#define CLI_ARGS_MAX          8


typedef struct
{
  uint16_t   argc;                              // 명령 이름 뒤에 붙은 단어 개수
  char     **argv;                              // 그 단어들

  int32_t  (*getData)(uint8_t index);           // 숫자로 읽기 ("10", "0x1F" 모두 됨)
  char    *(*getStr)(uint8_t index);            // 글자 그대로
  bool     (*isStr)(uint8_t index, const char *p_str);   // 이 단어가 맞나?
} cli_args_t;


bool cliInit(void);
bool cliOpen(uint8_t ch, uint32_t baud);
bool cliMain(void);                             // 받은 글자를 처리한다. 계속 불러줘야 한다
bool cliAdd(const char *cmd_str, void (*p_func)(cli_args_t *));
void cliPrintf(const char *fmt, ...);
bool cliKeepLoop(void);                         // 반복 명령 중 아무 키나 누르면 false

// 받은 바이트를 CLI 보다 먼저 보는 RX hook 함수를 등록한다.
// RX hook 이 true 를 돌려주면 그 바이트는 hook 이 가져가고, CLI 는 못 본다.
// (Step 10: GUI 패킷(02 FD ...)을 CLI 에서 빼내 net 태스크로 보내는 데 쓴다)
void cliSetRxHook(bool (*hook)(uint8_t data));

#endif

#endif
