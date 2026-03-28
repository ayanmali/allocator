#ifndef ALLOCATOR
#define ALLOCATOR
/*
Orchestrates the three layers of the memory allocator.

In the middle-end (central free lists + transfer cache),
a size class's central free list should be lazily initialized,
i.e., it should only be created once it is explicitly requested.

struct of arrays pattern
*/
#include "central_free_list.hpp"
#include "page_heap.hpp"
#include "slabs.hpp"
#include "transfer_cache.hpp"

struct Allocator {
    public:
        Allocator() {
            const auto num_cpus = std::thread::hardware_concurrency();
            void* per_cpu_region = mmap();
            // split the region into Slabs (one per logical CPU)

        }
        ~Allocator() = default;

        void* allocate() {
            // fetch from cache
            // if not available, check transfer cache
            // if transfer cache insufficient, check central free list
            // if CFLs insufficient, request allocation from page heap
        }

        void deallocate(void* mem) {
            // place back into cache
            // if cache is full, place in transfer cache
            // if transfer cache is full, place in CFL
            // if CFLs are full, send it back to the page heap
        }

    private:
        PageHeap* page_heap;
        CentralFreeList free_list[NUM_SIZE_CLASSES];
        TransferCache transfer_caches[NUM_SIZE_CLASSES];
        Slab* per_cpu_caches[];

};
#endif