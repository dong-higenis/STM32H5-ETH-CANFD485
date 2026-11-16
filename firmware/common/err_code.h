/**
 * @brief Baram님의 g4-fdcan내 사용되어진 프로토콜 응답에 대한 err_code값이다.
 * 추 후 툴과 값이 같아야 하므로 기존값은 절대로 바꾸면 안됀다.
 * 새 코드를 들여와야하면 빈곳에 추가만 하도록 한다.
 */
#ifndef COMMON_ERR_CODE_H_
#define COMMON_ERR_CODE_H_


#define OK                            0x0000
#define ERR_MEMORY                    0x0001
#define ERR_FULL                      0x0002
#define ERR_EMPTY                     0x0003
#define ERR_NULL                      0x0004
#define ERR_INVAILD_INDEX             0x0005

#define ERR_BOOT_WRONG_CMD            0x000A
#define ERR_BOOT_LED                  0x000B
#define ERR_BOOT_FLASH_ERASE          0x000C
#define ERR_BOOT_WRONG_RANGE          0x000D
#define ERR_BOOT_FLASH_WRITE          0x000E
#define ERR_BOOT_BUF_OVF              0x000F
#define ERR_BOOT_INVALID_FW           0x0010
#define ERR_BOOT_FW_CRC               0x0011
#define ERR_BOOT_JUMP_TO_FW           0x0012
#define ERR_BOOT_TAG_MAGIC            0x0013
#define ERR_BOOT_TAG_SIZE             0x0014
#define ERR_BOOT_FLASH_READ           0x0015

#define ERR_CMD_MAX_LENGTH            0x0020
#define ERR_CMD_CHECKSUM              0x0021
#define ERR_CMD_RX_TIMEOUT            0x0022
#define ERR_CMD_NO_CMD                0x0023

#define ERR_CAN_OPEN                  0x0040    // CAN 을 열지 못함 (잘못된 속도 조합 등)
#define ERR_CAN_NOT_OPEN              0x0041    // CAN 이 열려 있지 않음
#define ERR_CAN_FILTER                0x0042


#endif
