/*
 * fault.c  -  앱이 죽을 때 "왜, 어디서" 를 기록하고 다시 켜기 (Step 11)
 *
 *  전에는 HardFault 가 나면 while(1) 로 멈춰 있었다. 랜도 CAN 도 다 멈춘 채로.
 *  이제는
 *    1) 원인(종류, PC, LR, CFSR, BFAR, 태스크 이름)을 boot_shared 에 적고
 *    2) J1 UART 로 한 줄 찍고
 *    3) 리셋한다 -> 부트로더가 기록을 보여주고 앱을 다시 띄운다.
 *
 *  PC 를 아는 방법
 *    CPU 는 fault 가 나면 그 순간의 레지스터 8개(r0~r3, r12, lr, pc, xPSR)를 스택에 쌓는다.
 *    태스크 안에서 났으면 PSP(태스크 스택), 인터럽트 안이면 MSP 에 쌓인다.
 *    어느 쪽인지는 LR(EXC_RETURN)의 비트 2 가 알려준다.
 *    -> 아래 어셈블리 4줄이 그 스택 주소를 faultHandler() 에 넘겨준다.
 *
 *  PC 값으로 코드 위치 찾기:  arm-none-eabi-addr2line -e build/h5-fw.elf 0x0801xxxx
 */
#include "fault.h"

#ifdef _USE_HW_FAULT

#include "FreeRTOS.h"
#include "task.h"


#define RAM_START     0x20000000
#define RAM_END       0x200A0000          // 640KB


void faultHandler(uint32_t *frame, uint32_t type) __attribute__((used, noreturn));
static void faultPuts(const char *str);
static void faultPutHex(uint32_t value);


// ---- fault 처리기: 스택 주소(r0)와 종류(r1)를 들고 faultHandler 로 ----
#define FAULT_ENTRY(name, type_num)               \
  __attribute__((naked)) void name(void)          \
  {                                               \
    __asm volatile(                               \
      "tst   lr, #4          \n"  /* 비트2: 0=MSP, 1=PSP */ \
      "ite   eq              \n"                  \
      "mrseq r0, msp         \n"                  \
      "mrsne r0, psp         \n"                  \
      "mov   r1, %0          \n"                  \
      "b     faultHandler    \n"                  \
      : : "i"(type_num));                         \
  }

FAULT_ENTRY(HardFault_Handler,  FAULT_HARD)
FAULT_ENTRY(MemManage_Handler,  FAULT_MEM)
FAULT_ENTRY(BusFault_Handler,   FAULT_BUS)
FAULT_ENTRY(UsageFault_Handler, FAULT_USAGE)
FAULT_ENTRY(NMI_Handler,        FAULT_NMI)


void faultHandler(uint32_t *frame, uint32_t type)
{
  uint32_t pc = 0;
  uint32_t lr = 0;
  uint32_t cfsr = SCB->CFSR;

  // 스택을 쌓다가 난 fault(스택 넘침)면 frame 이 엉터리일 수 있다. RAM 안일 때만 읽는다.
  bool stack_err = (cfsr & (SCB_CFSR_STKERR_Msk | SCB_CFSR_MSTKERR_Msk | SCB_CFSR_STKOF_Msk)) != 0;
  if (!stack_err && (uint32_t)frame >= RAM_START && (uint32_t)frame + 32 <= RAM_END)
  {
    lr = frame[5];
    pc = frame[6];
  }

  faultReset(type, pc, lr, NULL);
}

void faultAssert(uint32_t line)
{
  faultReset(FAULT_ASSERT, (uint32_t)__builtin_return_address(0), line, NULL);
}

void faultReset(uint32_t type, uint32_t pc, uint32_t lr, const char *task)
{
  char name[16] = {0};

  __disable_irq();

  // 태스크 이름: 넘겨준 게 없으면 "지금 돌던 태스크"
  if (task == NULL && xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    task = pcTaskGetName(NULL);
  if (task == NULL)
    task = "(no task)";
  strncpy(name, task, sizeof(name) - 1);

  bootSharedCheck();
  boot_shared.fault_type   = type;
  boot_shared.fault_pc     = pc;
  boot_shared.fault_lr     = lr;
  boot_shared.fault_cfsr   = SCB->CFSR;
  boot_shared.fault_bfar   = SCB->BFAR;
  boot_shared.fault_uptime = HAL_GetTick();
  memcpy((void *)boot_shared.fault_task, name, sizeof(name));
  boot_shared.fault_count++;
  boot_shared.fault_magic  = FAULT_MAGIC_NEW;

  // 인터럽트가 꺼졌으니 UART 레지스터에 직접 한 글자씩 쓴다
  faultPuts("\r\n\r\n!! fault : ");
  faultPuts(faultTypeName(type));
  faultPuts(", task ");
  faultPuts(name);
  faultPuts(", pc ");
  faultPutHex(pc);
  faultPuts(" -> reset\r\n");

  uint32_t timeout = 100000;
  while (!(USART1->ISR & USART_ISR_TC) && --timeout);  // 마지막 글자가 선을 다 빠져나갈 때까지

  NVIC_SystemReset();
  while (1);
}


static void faultPutc(char c)
{
  uint32_t timeout = 100000;

  USART1->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;            // 인터럽트 송신은 그만
  while (!(USART1->ISR & USART_ISR_TXE_TXFNF) && --timeout);
  USART1->TDR = (uint8_t)c;
}

static void faultPuts(const char *str)
{
  while (*str)
    faultPutc(*str++);
}

static void faultPutHex(uint32_t value)
{
  faultPuts("0x");
  for (int i = 28; i >= 0; i -= 4)
    faultPutc("0123456789ABCDEF"[(value >> i) & 0xF]);
}

#endif
