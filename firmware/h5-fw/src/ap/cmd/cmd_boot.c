#include "cmd_boot.h"
#include "util.h"
#include "boot_shared.h"


/**
 * @brief BOOT 계열 명령 처리 (정보, 버전, LED, 펌웨어 업데이트)
 *
 *  명령 번호와 응답 형식은 Baram G4 펌웨어 / PC GUI(lib/cmd_boot.py, down_firm.py)와 같다.
 *
 *  펌웨어 업데이트 순서 (GUI 의 down_firm.py 가 이 순서로 보낸다)
 *    FW_BEGIN  [파일이름 64][크기 4]       시작 알림
 *    FW_ERASE  [주소 4][길이 4]            업데이트 영역 지우기
 *    FW_WRITE  [주소 4][길이 4][데이터]    256 바이트씩 쓰기 (주소 0x400 부터)
 *    FW_WRITE  [0][20][TAG]                맨 마지막에 TAG 쓰기
 *    FW_VERIFY                             TAG 의 CRC 와 실제 내용이 맞나
 *    FW_END
 *    FW_UPDATE                             부트로더에 복사 요청을 남기고 리셋
 *
 *  주소는 모두 업데이트 영역(FLASH_ADDR_UPDATE = 0x08080000) 안에서의 위치다.
 *  지금 돌고 있는 앱 영역(Bank1)은 건드리지 않으므로, 받다가 끊겨도 지금 앱은 멀쩡하다.
 */


#define BOOT_CMD_INFO       0x0000
#define BOOT_CMD_VERSION    0x0001
#define BOOT_CMD_FW_VER     0x0006
#define BOOT_CMD_FW_ERASE   0x0007
#define BOOT_CMD_FW_WRITE   0x0008
#define BOOT_CMD_FW_READ    0x0009
#define BOOT_CMD_FW_VERIFY  0x000A
#define BOOT_CMD_FW_UPDATE  0x000B
#define BOOT_CMD_FW_JUMP    0x000C
#define BOOT_CMD_FW_BEGIN   0x000D
#define BOOT_CMD_FW_END     0x000E
#define BOOT_CMD_LED        0x0010


typedef struct
{
  uint32_t mode;            // 0 = 부트로더가 응답 중, 1 = 앱이 응답 중
} boot_info_t;

typedef struct
{
  firm_ver_t boot;          // 부트로더 버전
  firm_ver_t firm;          // 지금 돌고 있는 앱 버전
  firm_ver_t update;        // 업데이트 영역(Bank2)에 받아 둔 앱 버전 (없으면 0)
} boot_version_t;

typedef struct
{
  char     fw_name[64];
  uint32_t fw_size;
} boot_begin_t;


static void     bootInfo(cmd_t *p_cmd);
static void     bootVersion(cmd_t *p_cmd);
static void     bootLed(cmd_t *p_cmd);
static void     bootFwVer(cmd_t *p_cmd);
static void     bootFwBegin(cmd_t *p_cmd);
static void     bootFwEnd(cmd_t *p_cmd);
static void     bootFwErase(cmd_t *p_cmd);
static void     bootFwWrite(cmd_t *p_cmd);
static void     bootFwRead(cmd_t *p_cmd);
static void     bootFwVerify(cmd_t *p_cmd);
static void     bootFwUpdate(cmd_t *p_cmd);
static uint16_t bootVerifyUpdateSlot(void);
static bool     getAddrLen(cmd_t *p_cmd, uint32_t *p_addr, uint32_t *p_len);


/**
 * @brief BOOT 명령이면 처리하고 true, 아니면 false
 */
bool cmdBootProcess(cmd_t *p_cmd)
{
  if (p_cmd->packet.type != PKT_TYPE_CMD)
    return false;

  switch (p_cmd->packet.cmd)
  {
    case BOOT_CMD_INFO:      bootInfo(p_cmd);     return true;
    case BOOT_CMD_VERSION:   bootVersion(p_cmd);  return true;
    case BOOT_CMD_LED:       bootLed(p_cmd);      return true;
    case BOOT_CMD_FW_VER:    bootFwVer(p_cmd);    return true;
    case BOOT_CMD_FW_BEGIN:  bootFwBegin(p_cmd);  return true;
    case BOOT_CMD_FW_END:    bootFwEnd(p_cmd);    return true;
    case BOOT_CMD_FW_ERASE:  bootFwErase(p_cmd);  return true;
    case BOOT_CMD_FW_WRITE:  bootFwWrite(p_cmd);  return true;
    case BOOT_CMD_FW_READ:   bootFwRead(p_cmd);   return true;
    case BOOT_CMD_FW_VERIFY: bootFwVerify(p_cmd); return true;
    case BOOT_CMD_FW_UPDATE: bootFwUpdate(p_cmd); return true;
    case BOOT_CMD_FW_JUMP:   cmdSendResp(p_cmd, p_cmd->packet.cmd, CMD_OK, NULL, 0); return true;
    default:                 return false;       // 내 명령이 아님
  }
}

/**
 * @brief 지금 누가 응답하는지 알려 준다. (앱이므로 mode = 1)
 */
static void bootInfo(cmd_t *p_cmd)
{
  boot_info_t info = {.mode = 1};

  cmdSendResp(p_cmd, p_cmd->packet.cmd, CMD_OK, (uint8_t *)&info, sizeof(info));
}

/**
 * @brief 부트로더 / 앱 / 업데이트 영역의 버전을 한 번에 보낸다.
 *        각 이미지의 버전 정보는 "이미지 시작 + 0x400" 에 있다.
 */
static void bootVersion(cmd_t *p_cmd)
{
  const firm_ver_t *p_boot   = (const firm_ver_t *)(FLASH_ADDR_BOOT   + FLASH_SIZE_VER);
  const firm_ver_t *p_firm   = (const firm_ver_t *)(FLASH_ADDR_FIRM   + FLASH_SIZE_TAG + FLASH_SIZE_VER);
  const firm_ver_t *p_update = (const firm_ver_t *)(FLASH_ADDR_UPDATE + FLASH_SIZE_TAG + FLASH_SIZE_VER);
  boot_version_t    ver;

  memset(&ver, 0, sizeof(ver));

  if (p_boot->magic_number == VERSION_MAGIC_NUMBER)   ver.boot   = *p_boot;
  if (p_firm->magic_number == VERSION_MAGIC_NUMBER)   ver.firm   = *p_firm;
  if (p_update->magic_number == VERSION_MAGIC_NUMBER) ver.update = *p_update;

  cmdSendResp(p_cmd, p_cmd->packet.cmd, CMD_OK, (uint8_t *)&ver, sizeof(ver));
}

/**
 * @brief 지금 돌고 있는 앱의 버전을 보낸다.
 */
static void bootFwVer(cmd_t *p_cmd)
{
  cmdSendResp(p_cmd, p_cmd->packet.cmd, CMD_OK,
              (const uint8_t *)(FLASH_ADDR_FIRM + FLASH_SIZE_TAG + FLASH_SIZE_VER), sizeof(firm_ver_t));
}

/**
 * @brief 보드를 찾기 쉽게 상태 LED 를 몇 번 깜빡인다. (GUI 의 LED 버튼)
 */
static void bootLed(cmd_t *p_cmd)
{
  cmdSendResp(p_cmd, p_cmd->packet.cmd, CMD_OK, NULL, 0);

  for (int i = 0; i < 6; i++)
  {
    ledToggle(HW_LED_CH_STATUS);
    delay(50);
  }
}

/**
 * @brief 업데이트 시작. 파일 크기가 업데이트 영역에 들어가는지 확인한다.
 */
static void bootFwBegin(cmd_t *p_cmd)
{
  boot_begin_t begin;
  uint16_t     err = CMD_OK;

  if (p_cmd->packet.length == sizeof(boot_begin_t))
  {
    memcpy(&begin, p_cmd->packet.data, sizeof(begin));
    begin.fw_name[sizeof(begin.fw_name) - 1] = 0;
    if (begin.fw_size + FLASH_SIZE_TAG > FLASH_SIZE_UPDATE)
      err = ERR_BOOT_WRONG_RANGE;
    logPrintf("fw : begin \"%s\" %d bytes\r\n", begin.fw_name, (int)begin.fw_size);
  }
  else
  {
    err = ERR_BOOT_WRONG_RANGE;
  }
  cmdSendResp(p_cmd, p_cmd->packet.cmd, err, NULL, 0);
}

/**
 * @brief 업데이트 끝 알림 (응답만 한다)
 */
static void bootFwEnd(cmd_t *p_cmd)
{
  logPrintf("fw : end\r\n");
  cmdSendResp(p_cmd, p_cmd->packet.cmd, CMD_OK, NULL, 0);
}

/**
 * @brief 업데이트 영역의 [주소][길이] 영역을 지운다.
 */
static void bootFwErase(cmd_t *p_cmd)
{
  uint32_t addr, len;
  uint16_t err = CMD_OK;
  uint32_t pre_time = millis();

  if (getAddrLen(p_cmd, &addr, &len) != true)
    err = ERR_BOOT_WRONG_RANGE;
  else if (flashErase(FLASH_ADDR_UPDATE + addr, len) != true)
    err = ERR_BOOT_FLASH_ERASE;

  logPrintf("fw : erase 0x%X %d bytes -> %s (%d ms)\r\n", (unsigned)addr, (int)len,
            err == CMD_OK ? "OK" : "NG", (int)(millis() - pre_time));
  cmdSendResp(p_cmd, p_cmd->packet.cmd, err, NULL, 0);
}

/**
 * @brief 업데이트 영역에 데이터를 쓰고, 다시 읽어 맞는지 비교한다.
 */
static void bootFwWrite(cmd_t *p_cmd)
{
  uint32_t addr, len;
  uint16_t err = CMD_OK;

  if (getAddrLen(p_cmd, &addr, &len) != true || p_cmd->packet.length < 8 + len)
  {
    err = ERR_BOOT_WRONG_RANGE;
  }
  else if (flashWrite(FLASH_ADDR_UPDATE + addr, &p_cmd->packet.data[8], len) != true)
  {
    err = ERR_BOOT_FLASH_WRITE;
  }
  else if (memcmp((const void *)(FLASH_ADDR_UPDATE + addr), &p_cmd->packet.data[8], len) != 0)
  {
    err = ERR_BOOT_FLASH_WRITE;                 // 써 놓고 다시 읽어 비교
  }
  cmdSendResp(p_cmd, p_cmd->packet.cmd, err, NULL, 0);
}

/**
 * @brief 업데이트 영역의 [주소][길이] 영역을 읽어 보낸다.
 */
static void bootFwRead(cmd_t *p_cmd)
{
  uint32_t addr, len;

  if (getAddrLen(p_cmd, &addr, &len) != true || len > CMD_MAX_DATA_LENGTH)
  {
    cmdSendResp(p_cmd, p_cmd->packet.cmd, ERR_BOOT_WRONG_RANGE, NULL, 0);
    return;
  }
  cmdSendResp(p_cmd, p_cmd->packet.cmd, CMD_OK, (const uint8_t *)(FLASH_ADDR_UPDATE + addr), len);
}

/**
 * @brief 업데이트 영역의 TAG 와 실제 내용이 맞는지 검사해 결과를 보낸다.
 */
static void bootFwVerify(cmd_t *p_cmd)
{
  uint16_t err = bootVerifyUpdateSlot();

  logPrintf("fw : verify -> %s (0x%04X)\r\n", err == CMD_OK ? "OK" : "NG", err);
  cmdSendResp(p_cmd, p_cmd->packet.cmd, err, NULL, 0);
}

/**
 * @brief 부트로더에 "업데이트 영역의 새 앱을 복사해 줘" 요청을 남기고 리셋한다.
 *        망가진 이미지를 복사하지 않도록, 리셋 전에 한 번 더 검사한다.
 */
static void bootFwUpdate(cmd_t *p_cmd)
{
  uint16_t err = bootVerifyUpdateSlot();

  cmdSendResp(p_cmd, p_cmd->packet.cmd, err, NULL, 0);
  if (err != CMD_OK)
    return;

  logPrintf("fw : update requested -> reset\r\n");
  delay(100);

  bootSharedCheck();
  boot_shared.request |= BOOT_REQ_UPDATE;
  resetToReset();
}


/**
 * @brief 업데이트 영역의 TAG 와 실제 내용(CRC)이 맞는지 검사한다.
 * @return CMD_OK, 아니면 에러 코드
 */
static uint16_t bootVerifyUpdateSlot(void)
{
  const firm_tag_t *p_tag = (const firm_tag_t *)FLASH_ADDR_UPDATE;
  uint16_t          crc;

  if (p_tag->magic_number != TAG_MAGIC_NUMBER)
    return ERR_BOOT_TAG_MAGIC;
  if (p_tag->fw_addr != FLASH_SIZE_TAG || p_tag->fw_size == 0 ||
      p_tag->fw_size > FLASH_SIZE_UPDATE - FLASH_SIZE_TAG)
    return ERR_BOOT_TAG_SIZE;

  crc = utilCrc16(0, (const uint8_t *)(FLASH_ADDR_UPDATE + p_tag->fw_addr), p_tag->fw_size);
  if ((uint16_t)p_tag->fw_crc != crc)
    return ERR_BOOT_FW_CRC;

  return CMD_OK;
}


/**
 * @brief 패킷 data 에서 [주소 4][길이 4] 를 꺼내고, 업데이트 영역 안인지 확인한다.
 */
static bool getAddrLen(cmd_t *p_cmd, uint32_t *p_addr, uint32_t *p_len)
{
  if (p_cmd->packet.length < 8)
    return false;

  memcpy(p_addr, &p_cmd->packet.data[0], 4);
  memcpy(p_len,  &p_cmd->packet.data[4], 4);

  return (*p_addr < FLASH_SIZE_UPDATE) && (*p_len <= FLASH_SIZE_UPDATE - *p_addr);
}
