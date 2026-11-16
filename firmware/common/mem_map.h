/*
 * mem_map.h
 *
 *  boot 와 fw 간 공유되는 메모리 맵
 *  bootloader 와 firmware사이 메모리 주소들만큼은 반드시 같아야 한다.
 *  값을 바꾸면 boot / fw 의 링커 스크립트(.ld)도 함께 바꿔야 한다.
 *  
 *  ------------------------------------------------------------------
 *  실제 보드의 MCU 내부 Flash : 1MB = Bank1(512KB) + Bank2(512KB), 섹터 8KB
 *  (STM32CubeProgrammer 확인: "Flash size : 1 MBytes", 2026-11-16)
 *  구버전 회로도의 STM32H563RIT6(2MB)가 아니다. 0x08100000 부터는 없는 주소 -> 읽으면 HardFault.
 *  ------------------------------------------------------------------
 *
 *  Bank1 0x08000000 +-------------------------+
 *                   | Bootloader        64KB  |  FLASH_ADDR_BOOT
 *        0x08010000 +-------------------------+
 *                   | Firmware 슬롯    448KB  |  FLASH_ADDR_FIRM
 *                   |  +0x000 TAG (이미지 헤더)|
 *                   |  +0x400 Vector table    |
 *                   |  +0x800 Version         |
 *  Bank2 0x08080000 +-------------------------+
 *                   | Update 슬롯      448KB  |  FLASH_ADDR_UPDATE (G4 보드의 SPI Flash 역할)
 *        0x080F0000 +-------------------------+
 *                   | Config           16KB   |  FLASH_ADDR_CONFIG (IP/CAN/RS485 설정)
 *        0x080F4000 +-------------------------+
 *                   | 예비             48KB   |
 *        0x08100000 +-------------------------+  <- Flash 끝
 */
#ifndef COMMON_MEM_MAP_H_
#define COMMON_MEM_MAP_H_


// 섹터(지우기 단위) 크기는 CMSIS 헤더(stm32h563xx.h)의 FLASH_SECTOR_SIZE = 8KB 를 그대로 쓴다.

#define FLASH_SIZE_TAG            0x400       // 이미지 헤더(TAG) 영역
#define FLASH_SIZE_VER            0x400       // Vector table 영역(=버전 구조체 위치 오프셋)

#define FLASH_ADDR_BOOT           0x08000000
#define FLASH_SIZE_BOOT           (64*1024)

#define FLASH_ADDR_FIRM           0x08010000
#define FLASH_SIZE_FIRM           (448*1024)

#define FLASH_ADDR_UPDATE         0x08080000
#define FLASH_SIZE_UPDATE         (448*1024)

#define FLASH_ADDR_CONFIG         0x080F0000
#define FLASH_SIZE_CONFIG         (16*1024)

#define FLASH_ADDR_END            0x08100000  // 이 주소부터는 Flash 가 없다


// RAM 앞 8KB 는 리셋해도 지우지 않는 영역(.noinit).
// boot 와 fw 가 fault 기록 등을 주고받는 데 쓴다.
#define RAM_ADDR_NOINIT           0x20000000
#define RAM_SIZE_NOINIT           (8*1024)


#endif
