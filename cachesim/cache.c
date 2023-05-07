#include "common.h"
#include <inttypes.h>
#include <string.h>

void mem_read(uintptr_t block_num, uint8_t *buf);
void mem_write(uintptr_t block_num, const uint8_t *buf);

static uint64_t cycle_cnt = 0;
static uint64_t r_cnt = 0;
static uint64_t r_hit_cnt = 0;
static uint64_t r_miss_cnt = 0;
static uint64_t r_replace_cnt = 0;
static uint64_t w_cnt = 0;
static uint64_t w_hit_cnt = 0;
static uint64_t w_miss_cnt = 0;
static uint64_t w_replace_cnt = 0;

void cycle_increase(int n) { cycle_cnt += n; }

// TODO: implement the following functions

#define REPLACE       "random"
#define WRITE_ALLOC   1
#define WRITE_BACK    1

#define NOT_VALID     0
#define VALID         1
#define DIRTY         2
#define VALID_DIRTY   3

// [63                addr                0]
// [63  tag  12][11  set  6][5  offset  0]
static uint8_t *cache = NULL;       // cache[set][way][offset]
static uintptr_t *tag = NULL;       // tag[set][way]
static uint8_t *valid_dirty = NULL; // valid_dirty[set][way]
static size_t total_size = 0;
static size_t associativity = 0;
static size_t set_size = 0;
static uintptr_t tag_mask = 0;      // tag = addr & tag_mask;
static uintptr_t set = 0;
static uintptr_t set_mask = 0;
static uintptr_t offset = 0;
static uintptr_t offset_mask = 0;

#define INDEX(addr)               ((addr & set_mask) >> BLOCK_WIDTH)
#define TAG(set)                ((uintptr_t *)(tag + sizeof(uintptr_t) * set * associativity))
#define V_D(set)                ((uint8_t *)(valid_dirty + set * associativity))
#define CACHE(set, way, offset) (cache + (set * associativity + way) * BLOCK_SIZE + offset)

// 从 cache 中读出 addr 地址处的 4 字节数据
// 若缺失，需要先从内存中读入数据
uint32_t cache_read(uintptr_t addr) {
  cycle_increase(1);
  addr = addr & ~0x3;
  r_cnt++;
  uintptr_t tag_addr = addr & tag_mask;
  set = INDEX(addr);
  offset = addr & offset_mask;
  printf("Read addr  = 0x%8lX \t set = 0x%4lX \t ", addr, set);
  for (size_t way = 0; way < associativity; way++)
  {
    if((TAG(set)[way] == tag_addr) && ((V_D(set)[way] & VALID) == VALID)) // hit
    {
      printf("way = %lu\n", way);
      r_hit_cnt++;
      cycle_increase(1);
      return *(uint32_t *)CACHE(set, way, offset);
    }
  }

  /**************** miss ***************/
  r_miss_cnt++;
  int way_empty = -1;
  int way_choose = -1;
  for (size_t i = 0; i < associativity; i++)
  {
    if((V_D(set)[i] & VALID) != VALID)
    {
      way_empty = i;
      break;
    }
  }
  if (way_empty == -1) // full
  {
    way_choose = choose(associativity); // random replace
    if((V_D(set)[way_choose] & DIRTY) == DIRTY) // dirty
    {
      r_replace_cnt++;
      uintptr_t block_write = (TAG(set)[way_choose] >> BLOCK_WIDTH) | set;
      printf("mem-write addr  = 0x%8lX, set = 0x%4lX, way = %d", block_write << BLOCK_WIDTH, INDEX(block_write << BLOCK_WIDTH), way_choose);
      mem_write(block_write, (uint8_t *)CACHE(set, way_choose, 0));
    }
  }
  else
  {
    way_choose = way_empty;
  }
  printf("\n");
  uintptr_t block_read = addr >> BLOCK_WIDTH;
  mem_read(block_read, (uint8_t *)CACHE(set, way_choose, 0));
  TAG(set)[way_choose] = tag_addr;
  V_D(set)[way_choose] = VALID;
  
  return *(uint32_t *)CACHE(set, way_choose, offset);
}

// 往 cache 中 addr 地址所属的块写入数据 data，写掩码为 wmask
// 例如当 wmask 为 0xff 时，只写入低8比特
// 若缺失，需要从先内存中读入数据
void cache_write(uintptr_t addr, uint32_t data, uint32_t wmask) {
  cycle_increase(1);
  addr = addr & ~0x3;
  w_cnt++;
  uintptr_t tag_addr = addr & tag_mask;
  set = INDEX(addr);
  offset = addr & offset_mask;
  printf("Write addr = 0x%8lX \t set = 0x%4lX \t", addr, set);
  for (size_t way = 0; way < associativity; way++)
  {
    if((TAG(set)[way] == tag_addr) && ((V_D(set)[way] & VALID) == VALID)) // hit
    {
      printf("way = %lu\n", way);
      w_hit_cnt++;
      cycle_increase(1);
      *(uint32_t *)CACHE(set, way, offset) = (data & wmask) | (*(uint32_t *)CACHE(set, way, offset) & ~wmask);
      V_D(set)[way] = VALID_DIRTY;
    }
  }

  /**************** miss ***************/
  w_miss_cnt++;
  int way_empty = -1;
  int way_choose = -1;
  for (size_t i = 0; i < associativity; i++)
  {
    if((V_D(set)[i] & VALID) != VALID)
    {
      way_empty = i;
      break;
    }
  }
  if (way_empty == -1) // full
  {
    way_choose = choose(associativity); // random replace
    if((V_D(set)[way_choose] & DIRTY) == DIRTY) // dirty
    {
      w_replace_cnt++;
      uintptr_t block_write = (TAG(set)[way_choose] >> BLOCK_WIDTH) | set;
      printf("mem-write addr  = 0x%8lX, set = 0x%4lX, way = %d", block_write << BLOCK_WIDTH, INDEX(block_write << BLOCK_WIDTH), way_choose);
      mem_write(block_write, (uint8_t *)CACHE(set, way_choose, 0));
    }
  }
  else
  {
    way_choose = way_empty;
  }
  printf("\n");
  uintptr_t block_read = addr >> BLOCK_WIDTH;
  mem_read(block_read, (uint8_t *)CACHE(set, way_choose, 0));
  TAG(set)[way_choose] = tag_addr;
  *(uint32_t *)CACHE(set, way_choose, offset) = (data & wmask) | (*(uint32_t *)CACHE(set, way_choose, offset) & ~wmask);
  V_D(set)[way_choose] = VALID_DIRTY;
}

// 初始化一个数据大小为 2^total_size_width B，关联度为 2^associativity_width 的 cache
// 例如 init_cache(14, 2) 将初始化一个 16KB，4 路组相联的cache
// 将所有 valid bit 置为无效即可
void init_cache(int total_size_width, int associativity_width) {
  int set_size_width = total_size_width - associativity_width - BLOCK_WIDTH;
  total_size = exp2(total_size_width);
  associativity = exp2(associativity_width);
  set_size = exp2(set_size_width);
  tag_mask = ~(uintptr_t)mask_with_len(set_size_width + BLOCK_WIDTH);
  set_mask = (uintptr_t)mask_with_len(set_size_width) << BLOCK_WIDTH;
  offset_mask = (uintptr_t)mask_with_len(BLOCK_WIDTH);

  cache = (uint8_t *)malloc(total_size);
  tag = (uintptr_t *)malloc(sizeof(uintptr_t) * associativity * set_size);
  valid_dirty = (uint8_t *)malloc(associativity * set_size);
  memset(valid_dirty, NOT_VALID, associativity * set_size);
}

void display_statistic(void) {
  printf("        Total count  \t Hit count (rate) \t Miss count (rate) \t Replace count\n");
  printf("Read:   %ld\t %ld(%f)\t %ld(%f)\t %ld\n", r_cnt, r_hit_cnt, (float)r_hit_cnt/r_cnt, r_miss_cnt, (float)r_miss_cnt/r_cnt, r_replace_cnt);
  printf("Write:  %ld\t %ld(%f)\t %ld(%f)\t %ld\n", w_cnt, w_hit_cnt, (float)w_hit_cnt/w_cnt, w_miss_cnt, (float)w_miss_cnt/r_cnt, w_replace_cnt);
  printf("Total:  %ld\t %ld(%f)\t %ld(%f)\t %ld\n", r_cnt + w_cnt, r_hit_cnt + w_hit_cnt, (float)(r_hit_cnt + w_hit_cnt)/(r_cnt + w_cnt), r_miss_cnt + w_miss_cnt, (float)(r_miss_cnt + w_miss_cnt)/(r_cnt + w_cnt), r_replace_cnt + w_replace_cnt);
}
