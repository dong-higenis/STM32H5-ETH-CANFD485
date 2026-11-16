/*
 * cli_task.c
 *
 *  터미널 CLI 를 돌리는 태스크 + 시스템 명령(info, task, reset).
 *
 *  1ms 마다 깨어나서 들어온 글자를 처리한다.
 *  CLI 타이핑에는 10ms 면 충분하지만, Step 10 부터 GUI 패킷도 이 경로로 들어오므로
 *  패킷이 늦게 넘어가지 않게 1ms 로 줄였다. (읽을 글자가 없으면 금방 끝나서 부담이 거의 없다)
 */
#include "cli_task.h"
#include "boot_shared.h"
#include "wdg_task.h"


extern const firm_ver_t firm_ver;     // hw.c 의 앱 버전 정보


static void cliTask(void *arg);
static void cliInfo(cli_args_t *args);
static void cliTaskList(cli_args_t *args);
static void cliReset(cli_args_t *args);


bool cliTaskInit(void)
{
  BaseType_t ret;

  cliAdd("info",  cliInfo);
  cliAdd("task",  cliTaskList);
  cliAdd("reset", cliReset);

  ret = xTaskCreate(cliTask, "cli", 1024, NULL, TASK_PRIO_NORMAL, NULL);  // 스택 4KB (printf 가 스택을 많이 씀)
  return (ret == pdPASS);
}

static void cliTask(void *arg)
{
  (void)arg;

  int8_t alive_id = aliveAdd("cli", 3000);    // 3초 넘게 루프를 못 돌면 "멈춤" (Step 11)

  cliOpen(HW_CLI_CH, HW_LOG_BAUD);

  while (1)
  {
    aliveKick(alive_id);
    cliMain();
    delay(1);
  }
}


// info : 보드, 부트로더, 앱 정보
static void cliInfo(cli_args_t *args)
{
  // Step 1 에서 정한 규칙: 부트로더 버전 정보는 항상 0x08000400 에 있다
  const firm_ver_t *p_boot = (const firm_ver_t *)(FLASH_ADDR_BOOT + FLASH_SIZE_VER);

  (void)args;

  cliPrintf("boot    : ");
  if (p_boot->magic_number == VERSION_MAGIC_NUMBER)
    cliPrintf("%s %s\n", p_boot->name_str, p_boot->version_str);
  else
    cliPrintf("(none)\n");

  cliPrintf("app     : %s %s\n", firm_ver.name_str, firm_ver.version_str);
  cliPrintf("clock   : %d MHz\n", (int)(HAL_RCC_GetSysClockFreq() / 1000000));
  cliPrintf("uptime  : %d s\n", (int)(millis() / 1000));
  cliPrintf("heap    : %d / %d bytes free (min %d)\n",
            (int)xPortGetFreeHeapSize(),
            (int)configTOTAL_HEAP_SIZE,
            (int)xPortGetMinimumEverFreeHeapSize());
}

// task : 지금 돌고 있는 태스크 목록
static void cliTaskList(cli_args_t *args)
{
  static TaskStatus_t list[16];
  UBaseType_t count;

  (void)args;

  count = uxTaskGetSystemState(list, ARRAY_COUNT(list), NULL);

  cliPrintf("name             state  prio  stack_free\n");
  for (UBaseType_t i = 0; i < count; i++)
  {
    const char *state;

    switch (list[i].eCurrentState)
    {
      case eRunning:   state = "run";   break;   // 지금 CPU 를 쓰는 중 (= 이 명령을 실행 중인 cli)
      case eReady:     state = "ready"; break;   // 돌 준비가 됐지만 차례를 기다리는 중
      case eBlocked:   state = "sleep"; break;   // vTaskDelay 등으로 잠든 상태
      case eSuspended: state = "stop";  break;
      default:         state = "?";     break;
    }

    cliPrintf("%-16s %-6s %4d  %6d bytes\n",
              list[i].pcTaskName,
              state,
              (int)list[i].uxCurrentPriority,
              (int)(list[i].usStackHighWaterMark * 4));   // word -> byte
  }
}

// reset : 보드 다시 시작 (부트로더부터 다시 돈다)
// reset      : 보드 다시 시작
// reset boot : 다시 시작하고 부트로더에 머물기 (앱으로 안 감)
static void cliReset(cli_args_t *args)
{
  if (args->argc == 1 && args->isStr(0, "boot"))
  {
    bootSharedCheck();
    boot_shared.request |= BOOT_REQ_STAY;   // 부트로더에 남기는 요청 (boot_shared)
    cliPrintf("reset to bootloader...\n");
  }
  else
  {
    cliPrintf("reset...\n");
  }
  delay(100);                 // 위 글자가 다 나갈 시간
  resetToReset();
}
