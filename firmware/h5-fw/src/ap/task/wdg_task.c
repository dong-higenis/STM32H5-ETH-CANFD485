/*
 * wdg_task.c  -  wdg(감시) 태스크 (Step 11)
 *
 *  동작 방식
 *    - net, cli 태스크는 자기 루프를 돌 때마다 aliveKick() 으로 살아 있음을 알린다
 *    - wdg 태스크는 100ms 마다 각 태스크의 마지막 aliveKick 시각을 확인한다.
 *        모두 최근에 aliveKick 했다 -> 하드웨어 워치독(IWDG)을 갱신(refresh)한다
 *        하나라도 3초 넘게 없다  -> "멈춤: 그 태스크" 라고 기록하고 리셋
 *    - wdg 태스크 자신이나 CPU 전체가 멈추면? IWDG 갱신이 끊기므로 4초 뒤 IWDG 가 리셋한다.
 *
 *  왜 IWDG 를 바로 갱신하지 않고 wdg 태스크를 두나?
 *    한 태스크(예: led)에서만 갱신하면, 다른 태스크(net)가 멈춰도 갱신은 계속된다.
 *    "모두 살아 있을 때만" 갱신해야 진짜 감시가 된다. 그리고 누가 멈췄는지 이름을 남길 수 있다.
 *
 *  wdg 태스크는 우선순위를 HIGH 로 둔다. NORMAL 태스크가 CPU 를 붙잡고 안 놓아도 wdg 태스크는 돈다.
 *
 *  연속 fault 횟수 (boot_shared.fault_count)
 *    앱이 30초 넘게 잘 돌면 0 으로 되돌린다. 3번 연속 fault 가 나면 부트로더가 앱으로 안 넘어간다.
 */
#include "wdg_task.h"
#include "boot_shared.h"


#define ALIVE_MAX           4
#define WDG_CHECK_MS        100


typedef struct
{
  const char       *name;
  uint32_t          limit_ms;
  volatile uint32_t last_ms;
  volatile bool     is_armed;      // 첫 aliveKick 이후부터 감시 (시작 준비가 오래 걸리는 태스크 배려)
} alive_t;


static alive_t alive[ALIVE_MAX];
static uint8_t alive_count = 0;


static void wdgTask(void *arg);
static void cliFault(cli_args_t *args);


bool wdgTaskInit(void)
{
  BaseType_t ret;

  cliAdd("fault", cliFault);

  ret = xTaskCreate(wdgTask, "wdg", 256, NULL, TASK_PRIO_HIGH, NULL);   // 스택 1KB
  return (ret == pdPASS);
}

int8_t aliveAdd(const char *name, uint32_t limit_ms)
{
  int8_t id = -1;

  // net, cli 태스크가 동시에 부를 수 있으므로 번호 나누기는 한 명씩
  taskENTER_CRITICAL();
  if (alive_count < ALIVE_MAX)
  {
    id = (int8_t)alive_count;
    alive[id].name     = name;
    alive[id].limit_ms = limit_ms;
    alive[id].is_armed = false;
    alive_count++;
  }
  taskEXIT_CRITICAL();

  return id;
}

void aliveKick(int8_t id)
{
  if (id < 0 || id >= alive_count)
    return;

  alive[id].last_ms  = millis();
  alive[id].is_armed = true;
}


static void wdgTask(void *arg)
{
  bool is_healthy = false;

  (void)arg;

  wdgBegin(HW_WDG_TIMEOUT_MS);
  logPrintf("wdg : IWDG %d ms, task stall limit check every %d ms\r\n", HW_WDG_TIMEOUT_MS, WDG_CHECK_MS);

  while (1)
  {
    uint32_t now = millis();

    for (int i = 0; i < alive_count; i++)
    {
      if (alive[i].is_armed && (now - alive[i].last_ms) > alive[i].limit_ms)
      {
        faultReset(FAULT_STALL, 0, now - alive[i].last_ms, alive[i].name);   // LR 필드 = 몇 ms 멈췄나
      }
    }

    wdgFeed();

    // 30초 넘게 잘 돌았다 -> 연속 fault 횟수를 0 으로
    if (!is_healthy && now >= FAULT_HEALTHY_MS)
    {
      is_healthy = true;
      boot_shared.fault_count = 0;
    }

    delay(WDG_CHECK_MS);
  }
}


// fault           : 마지막 고장 기록 보기
// fault clear     : 기록 지우기
// fault test xxx  : 일부러 죽어 보기 (bus / udf / assert / stall)
static void cliFault(cli_args_t *args)
{
  if (args->argc == 0 || (args->argc == 1 && args->isStr(0, "info")))
  {
    uint32_t magic = boot_shared.fault_magic;
    char     name[17] = {0};

    cliPrintf("fault count : %d in a row (boot stays after %d)\n", (int)boot_shared.fault_count, FAULT_COUNT_MAX);
    if (magic != FAULT_MAGIC_NEW && magic != FAULT_MAGIC_SEEN)
    {
      cliPrintf("last fault  : none\n");
    }
    else
    {
      memcpy(name, (const void *)boot_shared.fault_task, 16);
      cliPrintf("last fault  : %s\n", faultTypeName(boot_shared.fault_type));
      cliPrintf("  task      : %s\n", name);
      cliPrintf("  pc        : 0x%08X\n", (unsigned)boot_shared.fault_pc);
      cliPrintf("  lr        : 0x%08X\n", (unsigned)boot_shared.fault_lr);
      cliPrintf("  cfsr      : 0x%08X\n", (unsigned)boot_shared.fault_cfsr);
      cliPrintf("  bfar      : 0x%08X\n", (unsigned)boot_shared.fault_bfar);
      cliPrintf("  uptime    : %d ms\n", (int)boot_shared.fault_uptime);
    }
    cliPrintf("watch       :");
    for (int i = 0; i < alive_count; i++)
    {
      cliPrintf(" %s(%s %dms)", alive[i].name, alive[i].is_armed ? "ok" : "wait",
                alive[i].is_armed ? (int)(millis() - alive[i].last_ms) : 0);
    }
    cliPrintf("\n");
    return;
  }

  if (args->argc == 1 && args->isStr(0, "clear"))
  {
    boot_shared.fault_magic = 0;
    boot_shared.fault_count = 0;
    cliPrintf("fault : cleared\n");
    return;
  }

  if (args->argc == 2 && args->isStr(0, "test"))
  {
    if (args->isStr(1, "bus"))
    {
      // Step 6 에서 실제로 겪은 일: Flash 끝(1MB) 너머를 읽으면 BusFault
      cliPrintf("read 0x08100800 ...\n");
      delay(50);
      volatile uint32_t v = *(volatile uint32_t *)(FLASH_ADDR_END + 0x800);
      (void)v;
    }
    else if (args->isStr(1, "udf"))
    {
      cliPrintf("undefined instruction ...\n");
      delay(50);
      __asm volatile("udf #0");
    }
    else if (args->isStr(1, "assert"))
    {
      cliPrintf("configASSERT(0) ...\n");
      delay(50);
      configASSERT(0);
    }
    else if (args->isStr(1, "stall"))
    {
      // cli 태스크가 10초 동안 aliveKick 을 안 한다 -> wdg 태스크가 3초 뒤 "멈춤: cli" 로 리셋
      cliPrintf("cli task sleeps 10 s without kick ...\n");
      for (int i = 0; i < 100; i++)
        delay(100);
      cliPrintf("still alive? (should not see this)\n");
    }
    return;
  }

  cliPrintf("fault\n");
  cliPrintf("fault clear\n");
  cliPrintf("fault test bus|udf|assert|stall\n");
}
