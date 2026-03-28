#ifndef SIZE_CLASSES
#define SIZE_CLASSES

#include <cstddef>
#include <cstdint>

struct SizeClassInfo {
    uint32_t size; // max # of bytes this size class can store
    uint8_t pages; // # of pages to allocate at a time
    uint8_t batch_size; // # of objects that can be moved between a cache and a central list/transfer cache in one shot
    uint16_t byte_budget; // determines how many objects get cached per CPU for the size class
};

// {size, pages, batch_size, byte_budget}
// Sorted by size.  Index 0 is reserved for large allocations that
// bypass the slab / central free list path entirely.
static constexpr SizeClassInfo SizeClasses[] = {
  //   size  pg  batch  budget
  {      0,   0,    0,      0},  // large allocs bypass the slab
  {      8,   1,   32,  64000},  // -- tiny (8-64 B) --
  {     16,   1,   32,  64000},
  {     32,   1,   32,  64000},
  {     64,   1,   32,  64000},
  {     80,   1,   32,  32000},  // -- small (80-256 B) --
  {     96,   1,   32,  32000},
  {    112,   1,   32,  32000},
  {    128,   1,   32,  32000},
  {    160,   1,   32,  32000},
  {    176,   1,   32,  32000},
  {    208,   1,   32,  32000},
  {    256,   1,   32,  32000},
  {    312,   1,   32,  16000},  // -- medium (312-1024 B) --
  {    384,   1,   32,  16000},
  {    448,   1,   32,  16000},
  {    512,   1,   32,  16000},
  {    576,   1,   32,  16000},
  {    704,   1,   32,  16000},
  {    896,   1,   32,  16000},
  {   1024,   1,   32,  16000},
  {   1152,   2,   32,   8000},  // -- large (1152-4096 B) --
  {   1408,   2,   32,   8000},
  {   1792,   2,   32,   8000},
  {   2048,   2,   32,   8000},
  {   2688,   2,   24,   8000},
  {   3456,   3,   18,   8000},
  {   4096,   1,   16,   8000},
  {   4736,   3,   13,   4000},  // -- xl (4736-16384 B) --
  {   6144,   3,   10,   4000},
  {   8192,   1,    8,   4000},
  {   9472,   5,    6,   4000},
  {  12288,   3,    5,   4000},
  {  16384,   2,    4,   4000},
  {  20480,   5,    3,   2000},  // -- xxl (20480+ B) --
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

// Round a byte size up to the smallest size class that can hold it.
// Returns SizeClasses[0] (size == 0) if `in` exceeds the largest class,
// signalling a large allocation.
static SizeClassInfo round_size_class(size_t in) {
    if (in == 0) return SizeClasses[1];

    uint32_t lo = 1, hi = NUM_SIZE_CLASSES - 1;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (SizeClasses[mid].size < in)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (SizeClasses[lo].size >= in)
        return SizeClasses[lo];
    return SizeClasses[0];
}

static uint32_t size_class_to_idx(uint32_t size) {
    uint32_t lo = 0, hi = NUM_SIZE_CLASSES - 1;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (SizeClasses[mid].size < size)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

// static SizeClassInfo round_size_class_linear(size_t in) {
//     uint32_t i = 1;
//     while (i < NUM_SIZE_CLASSES) {
//         if (SizeClasses[i].size >= in && SizeClasses[i-1].size < in) {
//             return SizeClasses[i];
//         }
//         ++i;
//     }
//     return SizeClassInfo{};
// }

// static uint32_t size_class_to_idx_linear(SizeClassInfo sc) {
//     uint32_t i = 0;
//     while (i < NUM_SIZE_CLASSES) {
//         if (SizeClasses[i].size == sc.size) {
//             return i;
//         }
//         ++i;
//     }
//     return 0;
// }

#endif