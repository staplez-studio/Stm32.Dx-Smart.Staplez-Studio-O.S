/**
  ******************************************************************************
  * @file      sysmem.c
  * @brief     STM32CubeIDE System Memory calls file (sbrk heap allocator).
  ******************************************************************************
  */
#include <stddef.h>
#include <stdint.h>
#include <errno.h>

static uint8_t *__sbrk_heap_end = NULL;

void *_sbrk(ptrdiff_t incr)
{
  extern uint8_t _end;
  extern uint8_t _estack;
  extern uint32_t _Min_Stack_Size;
  const uint8_t *max_heap = (uint8_t*)&_estack - (uint32_t)&_Min_Stack_Size;
  uint8_t *prev_heap_end;

  if (__sbrk_heap_end == NULL) {
    __sbrk_heap_end = &_end;
  }

  if (__sbrk_heap_end + incr > max_heap) {
    errno = ENOMEM;
    return (void *)-1;
  }

  prev_heap_end = __sbrk_heap_end;
  __sbrk_heap_end += incr;

  return (void *)prev_heap_end;
}
