/*
 * main.c
 *
 *  전원 인가 후 실행 순서
 *    Reset_Handler (startup_stm32h563xx.s)
 *      -> SystemInit()   : FPU 켜기, VTOR 설정 (system_stm32h5xx.c)
 *      -> .data 복사, .bss 0 으로 채우기
 *      -> main()
 *           -> hwInit()  : bsp(클럭) + 보드 부품 드라이버
 *           -> apInit()  : 애플리케이션 준비
 *           -> apMain()  : 무한 루프
 */
#include "ap.h"


int main(void)
{
  hwInit();
  apInit();
  apMain();

  return 0;
}
