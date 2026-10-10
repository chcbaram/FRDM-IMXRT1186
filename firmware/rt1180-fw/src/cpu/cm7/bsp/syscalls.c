/*
 * newlib 시스템콜 최소 구현
 *
 * SDK 드라이버의 assert() → __assert_func → fiprintf 가 stdio 를 끌어오면서
 * nosys.specs 의 빈 스텁이 링크되고, ld 가 "_write is not implemented" 류
 * 경고를 낸다. 여기서 직접 정의해 경고를 없앤다.
 *
 * _write 는 출력을 버린다. CM7 은 UART 를 소유하지 않는다 (CM33 이 소유).
 * 힙(_sbrk)은 nosys 의 것을 그대로 쓴다 (링커 심볼 end 부터).
 */
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#undef errno
extern int errno;


__attribute__((weak)) int _write(int file, char *ptr, int len)
{
  (void)file;
  (void)ptr;

  return len;
}

int _read(int file, char *ptr, int len)
{
  (void)file;
  (void)ptr;
  (void)len;

  return 0;
}

int _close(int file)
{
  (void)file;

  return -1;
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

int _lseek(int file, int ptr, int dir)
{
  (void)file;
  (void)ptr;
  (void)dir;

  return 0;
}

int _getpid(void)
{
  return 1;
}

int _kill(int pid, int sig)
{
  (void)pid;
  (void)sig;

  errno = EINVAL;
  return -1;
}

void _exit(int status)
{
  (void)status;

  while (1)
  {
  }
}
