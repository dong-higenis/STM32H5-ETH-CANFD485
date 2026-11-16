# WIZnet ioLibrary (W6300)

출처: Baram `chcbaram/stm32h5-w6300` 저장소 (로컬 `C:\work\git\H5-W6300`, 커밋 c4e69a6, 2025-11-30)
의 `firmware/stm32h5-fw/src/hw/driver/wiznet` 폴더. 원본은 WIZnet ioLibrary_Driver (BSD/MIT 라이선스, 각 파일 머리말 참고).

가져온 파일: `wizchip_conf.*`, `socket/*`, `dhcp/*`, `soc/W6300/w6300.h`

바꾼 것 (1곳)
- `soc/W6300/w6300.h` 맨 끝의 `w6300Init()`, `w6300IsReady()` 선언 2줄 삭제.
  Baram 보드 전용 함수 선언이라, 이 프로젝트의 칩 접근 코드(`h5-fw/src/hw/driver/wiznet/w6300_port.c`)와 맞지 않는다.

바꾼 것 (2)
- `dhcp/dhcp.h` 의 `MAX_DHCP_RETRY`, `DHCP_WAIT_TIME` 에 `#ifndef` 를 씌웠다. (값은 그대로)
  빌드(CMakeLists.txt)에서 `DHCP_WAIT_TIME=3` 으로 바꿔 쓴다.
  이유: 랜선 연결 직후 첫 DHCP 요청이 스위치 준비 전에 없어지는 일이 있는데,
  원래 값(10초)이면 재부팅 후 IP 를 받기까지 20~40초가 걸렸다. (보드에서 확인, 2026-11-16)

가져오지 않은 것
- `soc/W6300/w6300.c` : 칩 접근 코드. 보드마다 다르므로 직접 작성했다 (`w6300_port.c`).
- `dns/`, `sntp/` : 지금은 쓰지 않는다.
