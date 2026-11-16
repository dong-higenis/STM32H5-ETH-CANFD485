/*
 * wdg.c  -  하드웨어 워치독 (IWDG)
 *
 *  IWDG 는 칩 안의 별도 시계(LSI, 약 32kHz)로 돈다. CPU 가 완전히 멈춰도(인터럽트까지) 동작한다.
 *  그래서 "소프트웨어가 아무것도 못 하는 상황"의 마지막 안전망이다.
 *
 *    32kHz / 64(분주) = 500Hz -> 1 카운트 = 2ms
 *    timeout 4000ms -> reload 2000
 *
 *  디버거로 CPU 를 멈춰 둔 동안에는 IWDG 도 멈추게 한다 (DBGMCU). 안 그러면 중단점에서 리셋된다.
 */
#include "wdg.h"

#ifdef _USE_HW_WDG


#define IWDG_KEY_RELOAD     0xAAAA
#define IWDG_KEY_ENABLE     0xCCCC
#define IWDG_KEY_ACCESS     0x5555

#define IWDG_LSI_HZ         32000
#define IWDG_PRESCALER      4         // PR=4 -> /64
#define IWDG_DIV            64


static bool is_running = false;


bool wdgBegin(uint32_t timeout_ms)
{
  uint32_t reload = (timeout_ms * (IWDG_LSI_HZ / IWDG_DIV)) / 1000;
  uint32_t pre_time;

  if (reload < 1)     reload = 1;
  if (reload > 0xFFF) reload = 0xFFF;

  DBGMCU->APB1FZR1 |= DBGMCU_APB1FZR1_DBG_IWDG_STOP;   // 디버거가 멈추면 IWDG 도 멈춤

  IWDG->KR  = IWDG_KEY_ENABLE;                        // 켜기 (LSI 도 자동으로 켜진다)
  IWDG->KR  = IWDG_KEY_ACCESS;                        // 설정 레지스터 잠금 풀기
  IWDG->PR  = IWDG_PRESCALER;
  IWDG->RLR = reload;

  // 설정이 IWDG 쪽 시계로 넘어갈 때까지 기다린다 (몇 ms)
  pre_time = millis();
  while ((IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0)
  {
    if (millis() - pre_time > 100)
      break;
  }

  IWDG->KR   = IWDG_KEY_RELOAD;
  is_running = true;
  return true;
}

void wdgFeed(void)
{
  if (is_running)
    IWDG->KR = IWDG_KEY_RELOAD;
}

bool wdgIsRunning(void)
{
  return is_running;
}

#endif
