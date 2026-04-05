#ifndef CONFIG
#define CONFIG
#include "size_classes.hpp"
#include <array>
#include <sys/types.h>

static constexpr size_t CACHE_LINE_SIZE = 64;
static constexpr uint16_t LOW_16_MASK = 0x0000FFFF;
static constexpr uint32_t HIGH_16_MASK = 0xFFFF0000;

static constexpr uint32_t  PAGE_SIZE = 8000; // 8 KB page size

static constexpr uint32_t MAX_PAGES = 255;

static constexpr uint32_t TRANSFER_CACHE_CAPACITY = 100;

// static constexpr size_t MMAP_THRESHOLD = 256000;
static constexpr uint32_t  MMAP_THRESHOLD = 400;

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