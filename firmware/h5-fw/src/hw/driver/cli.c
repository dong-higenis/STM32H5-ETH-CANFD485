/*
 * cli.c  -  터미널 CLI
 *
 *  글자가 들어오면
 *    일반 글자   -> 줄 버퍼에 쌓고 화면에 다시 보여준다(에코)
 *    Backspace  -> 한 글자 지운다
 *    Enter      -> 줄을 단어로 나누고, 첫 단어와 같은 이름의 명령 함수를 부른다
 *    ↑ 화살표    -> 바로 전 명령을 다시 불러온다
 *
 *  RX hook(rx_hook)이 등록돼 있으면 모든 바이트를 hook 이 먼저 본다. (cliSetRxHook)
 */
#include "cli.h"
#include "uart.h"

#ifdef _USE_HW_CLI


#define CLI_PROMPT      "cli# "

#define KEY_BS          0x08
#define KEY_DEL         0x7F
#define KEY_ESC         0x1B


typedef struct
{
  char cmd_str[CLI_CMD_NAME_MAX];
  void (*cmd_func)(cli_args_t *);
} cli_cmd_t;

typedef struct
{
  bool       is_open;
  uint8_t    ch;

  char       line[CLI_LINE_BUF_MAX];
  uint16_t   line_len;
  char       last_line[CLI_LINE_BUF_MAX];
  uint8_t    esc_state;
  char       pre_key;

  uint16_t   argc;
  char      *argv[CLI_ARGS_MAX];

  cli_cmd_t  cmd_list[CLI_CMD_LIST_MAX];
  uint16_t   cmd_count;

  cli_args_t args;

  bool     (*rx_hook)(uint8_t data);
} cli_t;


static cli_t cli;


static void    cliExecute(void);
static void    cliShowPrompt(void);
static int32_t cliArgsGetData(uint8_t index);
static char   *cliArgsGetStr(uint8_t index);
static bool    cliArgsIsStr(uint8_t index, const char *p_str);
static void    cliHelp(cli_args_t *args);


bool cliInit(void)
{
  memset(&cli, 0, sizeof(cli));

  cli.args.getData = cliArgsGetData;
  cli.args.getStr  = cliArgsGetStr;
  cli.args.isStr   = cliArgsIsStr;

  cliAdd("help", cliHelp);
  return true;
}

bool cliOpen(uint8_t ch, uint32_t baud)
{
  cli.ch = ch;
  if (uartIsOpen(ch) != true)
  {
    uartOpen(ch, baud);
  }
  cli.is_open = uartIsOpen(ch);

  if (cli.is_open)
    cliShowPrompt();
  return cli.is_open;
}

bool cliAdd(const char *cmd_str, void (*p_func)(cli_args_t *))
{
  if (cli.cmd_count >= CLI_CMD_LIST_MAX)
    return false;

  strncpy(cli.cmd_list[cli.cmd_count].cmd_str, cmd_str, CLI_CMD_NAME_MAX - 1);
  cli.cmd_list[cli.cmd_count].cmd_func = p_func;
  cli.cmd_count++;
  return true;
}

// "\n" 만 써도 터미널에서 줄이 제대로 바뀌도록 "\r\n" 으로 바꿔 보낸다
void cliPrintf(const char *fmt, ...)
{
  char    buf[256];
  char    out[512];
  int     len;
  int     o = 0;
  va_list args;

  va_start(args, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (len <= 0) return;
  len = cmin(len, (int)sizeof(buf) - 1);

  for (int i = 0; i < len && o < (int)sizeof(out) - 2; i++)
  {
    if (buf[i] == '\n' && (i == 0 || buf[i - 1] != '\r'))
      out[o++] = '\r';
    out[o++] = buf[i];
  }
  uartWrite(cli.ch, (uint8_t *)out, o);
}

void cliSetRxHook(bool (*hook)(uint8_t data))
{
  cli.rx_hook = hook;
}

bool cliKeepLoop(void)
{
  if (uartAvailable(cli.ch) > 0)
  {
    uartRead(cli.ch);
    return false;
  }
  return true;
}

bool cliMain(void)
{
  if (cli.is_open != true)
    return false;

  while (uartAvailable(cli.ch) > 0)
  {
    char key = (char)uartRead(cli.ch);

    // RX hook 이 가져간 바이트(GUI 패킷)는 CLI 가 보지 않는다
    if (cli.rx_hook != NULL && cli.rx_hook((uint8_t)key) == true)
      continue;

    // ---- 화살표 키: ESC [ A ----
    if (cli.esc_state == 1)
    {
      cli.esc_state = (key == '[') ? 2 : 0;
      continue;
    }
    if (cli.esc_state == 2)
    {
      cli.esc_state = 0;
      if (key == 'A' && cli.last_line[0] != 0)          // ↑ : 전 명령 불러오기
      {
        while (cli.line_len > 0)                         // 지금 줄 지우고
        {
          cliPrintf("\b \b");
          cli.line_len--;
        }
        strcpy(cli.line, cli.last_line);                 // 전 명령으로 채우기
        cli.line_len = strlen(cli.line);
        cliPrintf("%s", cli.line);
      }
      continue;
    }

    switch (key)
    {
      case KEY_ESC:
        cli.esc_state = 1;
        break;

      case '\r':
      case '\n':
        // 터미널이 Enter 를 "\r\n" 으로 보내면 두 번 실행되지 않게 "\n" 은 무시
        if (key == '\n' && cli.pre_key == '\r')
          break;
        cliPrintf("\n");
        cliExecute();
        cliShowPrompt();
        break;

      case KEY_BS:
      case KEY_DEL:
        if (cli.line_len > 0)
        {
          cli.line_len--;
          cliPrintf("\b \b");
        }
        break;

      default:
        if (key >= 0x20 && key < 0x7F && cli.line_len < CLI_LINE_BUF_MAX - 1)
        {
          cli.line[cli.line_len++] = key;
          uartWrite(cli.ch, (uint8_t *)&key, 1);         // 에코
        }
        break;
    }
    cli.pre_key = key;
  }

  return true;
}

static void cliShowPrompt(void)
{
  cliPrintf(CLI_PROMPT);
}

// 줄을 공백으로 잘라 단어로 만들고, 첫 단어 이름의 명령을 찾아 실행한다
static void cliExecute(void)
{
  char *tok;
  char *save;
  char *word[CLI_ARGS_MAX];
  uint16_t count = 0;

  cli.line[cli.line_len] = 0;
  if (cli.line_len > 0)
  {
    strcpy(cli.last_line, cli.line);
  }
  cli.line_len = 0;

  tok = strtok_r(cli.line, " ", &save);
  while (tok != NULL && count < CLI_ARGS_MAX)
  {
    word[count++] = tok;
    tok = strtok_r(NULL, " ", &save);
  }
  if (count == 0)
    return;

  for (int i = 0; i < cli.cmd_count; i++)
  {
    if (strcmp(word[0], cli.cmd_list[i].cmd_str) == 0)
    {
      cli.argc = count - 1;
      for (int a = 0; a < cli.argc; a++)
        cli.argv[a] = word[a + 1];

      cli.args.argc = cli.argc;
      cli.args.argv = cli.argv;
      cli.cmd_list[i].cmd_func(&cli.args);
      return;
    }
  }

  cliPrintf("Command Not Found : %s\n", word[0]);
}

static int32_t cliArgsGetData(uint8_t index)
{
  if (index >= cli.argc) return 0;
  return (int32_t)strtoul(cli.argv[index], NULL, 0);
}

static char *cliArgsGetStr(uint8_t index)
{
  if (index >= cli.argc) return "";
  return cli.argv[index];
}

static bool cliArgsIsStr(uint8_t index, const char *p_str)
{
  if (index >= cli.argc) return false;
  return (strcmp(p_str, cli.argv[index]) == 0);
}

static void cliHelp(cli_args_t *args)
{
  (void)args;

  cliPrintf("---------- cmd list ----------\n");
  for (int i = 0; i < cli.cmd_count; i++)
  {
    cliPrintf("  %s\n", cli.cmd_list[i].cmd_str);
  }
  cliPrintf("------------------------------\n");
}

#endif
