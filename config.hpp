#ifndef CONFIG
#define CONFIG
#include "size_classes.hpp"
#include <array>
#include <sys/types.h>

static constexpr size_t CACHE_LINE_SIZE = 64;
static constexpr uint16_t LOW_16_MASK = 0x0000FFFF;
static constexpr uint32_t HIGH_16_MASK = 0xFFFF0000;

// PageHeap stores span addresses as page IDs (address / PAGE_SIZE), so this
// must match the OS page size used by mmap on the target platform.
static constexpr uint32_t  PAGE_SIZE = 4096;

static constexpr uint32_t MAX_PAGES = 255;

static constexpr uint32_t TRANSFER_CACHE_CAPACITY = 100;

// static constexpr size_t MMAP_THRESHOLD = 256000;
static constexpr uint32_t  MMAP_THRESHOLD = 400;

static constexpr uint32_t MAX_SLAB_POINTERS = 20000;
static constexpr bool PAGE_ALIGN_SLAB_STRIDE = false;

// Each size class's pointer array capacity is derived from its byte_budget:
//   raw_slots = byte_budget / obj_size, rounded down to a batch_size multiple.
// If the total exceeds MAX_SLAB_POINTERS, all capacities are scaled down
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
  if (total > MAX_SLAB_POINTERS) {
      for (uint32_t i = 1; i < NUM_SIZE_CLASSES; ++i) {
          uint32_t batch = SizeClasses[i].batch_size;
          capacities[i] = (capacities[i] * MAX_SLAB_POINTERS / total);
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

static constexpr auto compute_slab_capacities() {
  std::array<uint32_t, NUM_SIZE_CLASSES> capacities{};
  for (uint32_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
      capacities[i] = SIZE_CLASS_OFFSETS[i + 1] - SIZE_CLASS_OFFSETS[i];
  }
  return capacities;
}

static constexpr auto SIZE_CLASS_CAPACITIES = compute_slab_capacities();

#endif