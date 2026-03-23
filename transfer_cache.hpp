/*
Allows memory to quickly move between various threads/frontend allocator and the central free lists.
One transfer cache per size class.

When the frontend attempts to allocate memory, and the thread cache is empty --> check the transfer cache for that size class
If the transfer cache is empty --> check central free list

When the frontend attempts to deallocate memory, and the thread cache is full --> attempt to place the object back into the transfer cache.
If the transfer cache doesn't have enough space --> put it into the central free list


*/

#include "central_free_list.hpp"
#include <span>

struct Batch {
    std::span<> idk;
    FreeObject* objs[];
};

/*
ring buffer
template parameter N represents the number of batches this transfer cache can hold.
*/
template <size_t N>
struct TransferCache {
    std::array<Batch, N> buffer;
    uint32_t read_idx;
    uint32_t write_idx;

    TransferCache() : buffer(nullptr), read_idx(0), write_idx(0) {
    }

    ~TransferCache() {

    }

    bool Push(Batch& batch) {
        buffer[write_idx] = batch;
        write_idx = (write_idx + 1) % N;
        return true;
    }
    Batch Pop() {
        if (read_idx > write_idx) {
            return Batch{};
        }
        auto old_read_idx = read_idx;
        ++read_idx;
        return buffer[old_read_idx];
    }

};