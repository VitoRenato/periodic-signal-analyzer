/**
  ******************************************************************************
  * @file    syscalls.c
  * @brief   GNU Arm 裸机环境所需的最小系统调用实现
  ******************************************************************************
  */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "main.h"  /* HAL_USART_Transmit, huart1 */

/* 链接脚本限定的 512 字节堆区；本工程当前不依赖动态内存。 */
extern uint8_t _end;
extern uint8_t _heap_end;

static uint8_t *g_heap_cursor;

void *_sbrk(ptrdiff_t increment)
{
  uintptr_t previous;
  uintptr_t heap_limit;
  uintptr_t amount;

  if (g_heap_cursor == NULL)
  {
    g_heap_cursor = &_end;
  }

  previous = (uintptr_t)g_heap_cursor;
  heap_limit = (uintptr_t)&_heap_end;

  if ((increment < 0) || (previous > heap_limit))
  {
    errno = ENOMEM;
    return (void *)-1;
  }

  amount = (uintptr_t)increment;
  if (amount > (heap_limit - previous))
  {
    errno = ENOMEM;
    return (void *)-1;
  }

  g_heap_cursor = (uint8_t *)(previous + amount);
  return (void *)previous;
}

int _close(int file)
{
  (void)file;
  errno = ENOSYS;
  return -1;
}

int _fstat(int file, struct stat *status)
{
  (void)file;

  if (status == NULL)
  {
    errno = EINVAL;
    return -1;
  }

  status->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int file)
{
  (void)file;
  return 1;
}

int _lseek(int file, int offset, int whence)
{
  (void)file;
  (void)offset;
  (void)whence;
  errno = ENOSYS;
  return -1;
}

int _read(int file, char *buffer, int length)
{
  (void)file;
  (void)buffer;
  (void)length;
  errno = ENOSYS;
  return -1;
}

int _write(int file, char *buffer, int length)
{
  (void)file;
  if (length <= 0) return 0;
  /* 阻塞发送，保证日志不丢。注意：中断上下文里不要用 printf。 */
  if (HAL_USART_Transmit(&huart1, (const uint8_t *)buffer, (uint16_t)length, HAL_MAX_DELAY) == HAL_OK) {
      return length;
  }
  errno = EIO;
  return -1;
}

int _getpid(void)
{
  return 1;
}

int _kill(int pid, int signal)
{
  (void)pid;
  (void)signal;
  errno = EINVAL;
  return -1;
}

void _exit(int status)
{
  (void)status;

  /* 裸机系统没有可返回的宿主环境。 */
  for (;;)
  {
  }
}
