/*
 * syscalls.c
 *
 *  newlib(C 표준 라이브러리)이 파일/콘솔을 다룰 때 부르는 OS 함수들.
 *  MCU 에는 OS 가 없으므로 "지원 안 함"으로 채워 둔다.
 *  (우리는 printf 대신 logPrintf/uartPrintf 를 쓰므로 실제로 불리지 않는다)
 *
 *  이 파일이 없으면 GCC 12+ 에서 "_write is not implemented" 같은 링크 경고가 난다.
 */
#include <errno.h>
#include <sys/stat.h>

#undef errno
extern int errno;


int _close(int file)
{
  (void)file;
  return -1;
}

int _lseek(int file, int ptr, int dir)
{
  (void)file; (void)ptr; (void)dir;
  return 0;
}

int _read(int file, char *ptr, int len)
{
  (void)file; (void)ptr; (void)len;
  return 0;
}

int _write(int file, char *ptr, int len)
{
  (void)file; (void)ptr;
  return len;
}

int _fstat(int file, struct stat *st)
{
  (void)file;
  st->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int file)
{
  (void)file;
  return 1;
}
