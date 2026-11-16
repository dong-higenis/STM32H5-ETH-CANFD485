/*
 * image.h
 *
 *  펌웨어 이미지 안에 들어가는 구조체.
 *  PC 툴(fdcan-gui)이 바이너리에서 직접 읽으므로 크기와 순서를 바꾸면 안 된다.
 *  (GUI: struct.unpack("<I32s32sI", bin[0x400:...]) )
 */
#ifndef COMMON_IMAGE_H_
#define COMMON_IMAGE_H_

#include <stdint.h>


#define VERSION_MAGIC_NUMBER      0x56455220    // "VER "
#define TAG_MAGIC_NUMBER          0x54414720    // "TAG "


// 각 이미지(boot, fw)의 "+0x400" 위치에 놓이는 버전 정보.
// 링커 스크립트의 .version 섹션이 이 위치를 보장한다.
typedef struct
{
  uint32_t magic_number;        // VERSION_MAGIC_NUMBER
  char     version_str[32];     // 예: "B261116R1"
  char     name_str[32];        // 예: "H5-ETH-FDCAN-BOOT"
  uint32_t firm_addr;           // 이 이미지가 링크된 시작 주소
} firm_ver_t;


// 펌웨어 슬롯 맨 앞(+0x000)에 놓이는 이미지 헤더.
// 부트로더는 이 값을 보고 "앱이 온전한가"를 판단한다.
//
//   슬롯 시작 ┬ TAG       (1KB, 이 구조체 + 0xFF 채움)
//      +0x400 ┼ 앱 이미지 (빌드 결과 .bin 그대로, 첫 부분이 Vector table)
//             ┴ ... fw_size 바이트
//
// 빌드 후 tools/fw_tag.py 가 만들어 붙이고, 네트워크 업데이트 때는 PC 툴이 만든다.
typedef struct
{
  uint32_t magic_number;        // TAG_MAGIC_NUMBER
  uint32_t fw_addr;             // 슬롯 시작에서 앱 이미지까지의 거리 (= 0x400)
  uint32_t fw_size;             // 앱 이미지 바이트 수
  uint32_t fw_crc;              // 앱 이미지 전체의 CRC16 (하위 16비트 사용)
  uint32_t tag_crc;             // 예약 (Baram 호환, 현재 0)
} firm_tag_t;


#endif
