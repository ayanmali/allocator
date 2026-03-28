#ifndef CONFIG
#define CONFIG
#include "size_classes.hpp"
#include <array>

static constexpr uint32_t  PAGE_SIZE = 8000; // 8 KB page size

static constexpr uint32_t MAX_PAGES = 255;

static constexpr uint32_t TRANSFER_CACHE_CAPACITY = 100;

// static constexpr size_t MMAP_THRESHOLD = 256000;
static constexpr uint32_t  MMAP_THRESHOLD = 400;

// {size, pages, batch_size, byte_budget}
// byte_budget: max bytes of object memory cached per CPU for this class.
// Smaller classes get higher budgets (more pointer slots) since they are
// allocated most frequently.  The slab offset computation scales these
// down proportionally if the total exceeds PER_CPU_SLAB_SIZE.
static constexpr SizeClassInfo SizeClasses[] = {
  //   size  pg  batch  budget
  {      0,   0,    0,      0},  // large allocs bypass the slab
  {      8,   1,   32,  64000},  // ── tiny (8-64 B) ──
  {     16,   1,   32,  64000},
  {     32,   1,   32,  64000},
  {     64,   1,   32,  64000},
  {     80,   1,   32,  32000},  // ── small (80-256 B) ──
  {     96,   1,   32,  32000},
  {    112,   1,   32,  32000},
  {    128,   1,   32,  32000},
  {    160,   1,   32,  32000},
  {    176,   1,   32,  32000},
  {    208,   1,   32,  32000},
  {    256,   1,   32,  32000},
  {    312,   1,   32,  16000},  // ── medium (312-1024 B) ──
  {    384,   1,   32,  16000},
  {    448,   1,   32,  16000},
  {    512,   1,   32,  16000},
  {    576,   1,   32,  16000},
  {    704,   1,   32,  16000},
  {    896,   1,   32,  16000},
  {   1024,   1,   32,  16000},
  {   1152,   2,   32,   8000},  // ── large (1152-4096 B) ──
  {   1408,   2,   32,   8000},
  {   1792,   2,   32,   8000},
  {   2048,   2,   32,   8000},
  {   2688,   2,   24,   8000},
  {   3456,   3,   18,   8000},
  {   4096,   1,   16,   8000},
  {   4736,   3,   13,   4000},  // ── xl (4736-16384 B) ──
  {   6144,   3,   10,   4000},
  {   8192,   1,    8,   4000},
  {   9472,   5,    6,   4000},
  {  12288,   3,    5,   4000},
  {  16384,   2,    4,   4000},
  {  20480,   5,    3,   2000},  // ── xxl (20480+ B) ──
  {  28672,   7,    2,   2000},
  {  32768,   4,    2,   2000},
  {  40960,   5,    2,   2000},
  {  49152,   6,    2,   2000},
  {  65536,   8,    2,   2000},
  {  73728,   9,    2,   2000},
  {  81920,  10,    2,   2000},
  {  98304,  12,    2,   2000},
  { 114688,  14,    2,   2000},
  { 131072,  16,    2,   2000},
  { 155648,  19,    2,   2000},
  { 204800,  25,    2,   2000},
  { 262144,  32,    2,   2000}
};

static constexpr uint32_t NUM_SIZE_CLASSES = sizeof(SizeClasses) / sizeof(SizeClasses[0]);

static constexpr uint32_t PER_CPU_SLAB_SIZE = 20000;

// Each size class's pointer array capacity is derived from its byte_budget:
//   raw_slots = byte_budget / obj_size, rounded down to a batch_size multiple.
// If the total exceeds PER_CPU_SLAB_SIZE, all capacities are scaled down
// proportionally (preserving batch alignment).
// Returns NUM_SIZE_CLASSES + 1 offsets so that class i owns
// [offsets[i], offsets[i+1]).
static constexpr auto compute_slab_offsets() {
  std::array<uint32_t, NUM_SIZE_CLASSES + 1> offsets{};
  std::array<uint32_t, NUM_SIZE_CLASSES> capacities{};
  uint32_t total = 0;

  for (uint32_t i = 1; i < NUM_SIZE_CLASSES; ++i) {
      uint32_t budget = SizeClasses[i].byte_budget;
      uint32_t batch  = SizeClasses[i].batch_size;
      uint32_t raw = budget / SizeClasses[i].size;
      uint32_t cap = (raw / batch) * batch;
      // round down to batch multiple; minimum 1 batch
      if (cap < batch) cap = batch;
      capacities[i] = cap;
      total += cap;
  }
  // Scale down if total exceeds slab size
  if (total > PER_CPU_SLAB_SIZE) {
      for (uint32_t i = 1; i < NUM_SIZE_CLASSES; ++i) {
          uint32_t batch = SizeClasses[i].batch_size;
          capacities[i] = (capacities[i] * PER_CPU_SLAB_SIZE / total);
          capacities[i] = (capacities[i] / batch) * batch;
          if (capacities[i] < batch) capacities[i] = batch;
      }
  }
  // Prefix sum
  offsets[0] = 0;
  for (uint32_t i = 1; i <= NUM_SIZE_CLASSES; ++i) {
      offsets[i] = offsets[i - 1] + capacities[i - 1];
  }
  return offsets;
}

static constexpr auto SIZE_CLASS_OFFSETS = compute_slab_offsets();

#endif