"""
fw_tag.py - 앱 이미지(.bin) 앞에 TAG(이미지 헤더)를 붙여 슬롯 이미지를 만든다.

  입력 : h5-fw.bin       (0x08010400 에 링크된 앱. 첫 부분이 Vector table)
  출력 : h5-fw_tag.bin   (TAG 1KB + 입력 이미지) -> 0x08010000 에 굽는다

  TAG 구조와 CRC 는 firmware/common/image.h, firmware/common/core/util.c 와 같아야 하고,
  PC 툴(fdcan-gui lib/down_firm.py)이 만드는 TAG 와도 같아야 한다.

사용:
  python fw_tag.py h5-fw.bin h5-fw_tag.bin [--name H5-ETH-FDCAN-FW] [--bad-crc]

  --bad-crc : 일부러 틀린 CRC 를 넣는다 (부트로더 검증 시험용)
"""
import argparse
import struct
import sys

TAG_MAGIC_NUMBER     = 0x54414720   # "TAG "
VERSION_MAGIC_NUMBER = 0x56455220   # "VER "
SIZE_TAG             = 0x400
SIZE_VER             = 0x400        # 이미지 안에서 버전 구조체가 놓인 위치
SIZE_FIRM_SLOT       = 448 * 1024    # 1MB 칩 (firmware/common/mem_map.h 의 FLASH_SIZE_FIRM)


def crc16(buf):
  """Baram CRC16: 다항식 0x8005, MSB 우선, 초기값 0 (표 없이 비트 단위로 계산)"""
  crc = 0
  for b in buf:
    crc ^= b << 8
    for _ in range(8):
      crc = ((crc << 1) ^ 0x8005) if (crc & 0x8000) else (crc << 1)
      crc &= 0xFFFF
  return crc


def main():
  ap = argparse.ArgumentParser()
  ap.add_argument("input")
  ap.add_argument("output")
  ap.add_argument("--name", default=None, help="버전 구조체의 이름에 이 문자열이 있는지 확인")
  ap.add_argument("--bad-crc", action="store_true", help="시험용: 틀린 CRC 를 기록")
  args = ap.parse_args()

  with open(args.input, "rb") as f:
    image = f.read()

  if len(image) + SIZE_TAG > SIZE_FIRM_SLOT:
    sys.exit("fw_tag: image too large (%d bytes)" % len(image))

  # 이미지 +0x400 의 버전 구조체 확인 (GUI 가 하는 검사와 같다)
  magic, ver, name, addr = struct.unpack_from("<I32s32sI", image, SIZE_VER)
  if magic != VERSION_MAGIC_NUMBER:
    sys.exit("fw_tag: version magic not found at +0x%X" % SIZE_VER)
  ver  = ver.split(b"\0")[0].decode()
  name = name.split(b"\0")[0].decode()
  if args.name and args.name not in name:
    sys.exit("fw_tag: name mismatch (%s)" % name)

  crc = crc16(image)
  if args.bad_crc:
    crc ^= 0xFFFF

  tag = struct.pack("<IIIII", TAG_MAGIC_NUMBER, SIZE_TAG, len(image), crc, 0)
  tag = tag + b"\xFF" * (SIZE_TAG - len(tag))

  with open(args.output, "wb") as f:
    f.write(tag + image)

  print("fw_tag: %s %s, size %d, crc 0x%04X%s, addr 0x%08X"
        % (name, ver, len(image), crc, " (BAD CRC for test)" if args.bad_crc else "", addr))


if __name__ == "__main__":
  main()
