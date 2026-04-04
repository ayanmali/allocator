/*
Allows memory to quickly move between various threads/frontend allocator and the central free lists.
One transfer cache per size class.

When the frontend attempts to allocate memory, and the thread cache is empty --> check the transfer cache for that size class
If the transfer cache is empty --> check central free list

When the frontend attempts to deallocate memory, and the thread cache is full --> attempt to place the object back into the transfer cache.
If the transfer cache doesn't have enough space --> put it into the central free list
*/
#ifndef TRANSFER_CACHE
#define TRANSFER_CACHE
#include "config.hpp"
#include <algorithm>
#include <cstring>
#include <mutex>

/*
Flat void* array protected by a mutex. Operates as a bounded stack:
slots [0, used_slots) are occupied. Each TransferCache corresponds to
one size class.
*/
struct TransferCache {
    TransferCache() : used_slots(0) {}
    ~TransferCache() = default;

    uint32_t InsertRange(void** batch, uint32_t n) {
        const std::lock_guard<std::mutex> lock(mu);
        uint32_t space = TRANSFER_CACHE_CAPACITY - used_slots;
        uint32_t to_insert = std::min(n, space);
        if (to_insert > 0) {
            std::memcpy(&slots[used_slots], batch, to_insert * sizeof(void*));
            used_slots += to_insert;
        }
        return to_insert;
    }

    uint32_t RemoveRange(void** batch, uint32_t n) {
        const std::lock_guard<std::mutex> lock(mu);
        uint32_t to_remove = std::min(n, used_slots);
        if (to_remove > 0) {
            used_slots -= to_remove;
            std::memcpy(batch, &slots[used_slots], to_remove * sizeof(void*));
        }
        return to_remove;
    }

    private:
    std::mutex mu;
    void* slots[TRANSFER_CACHE_CAPACITY];
    uint32_t used_slots;
};

#endif
