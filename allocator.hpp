#ifndef ALLOCATOR
#define ALLOCATOR
/*
Orchestrates the three layers of the memory allocator.

In the middle-end (central free lists + transfer cache),
a size class's central free list should be lazily initialized,
i.e., it should only be created once it is explicitly requested.

struct of arrays pattern
*/
#include <thread>
#include "central_free_list.hpp"
#include "page_heap.hpp"
#include "size_classes.hpp"
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

        void* allocate(size_t input_size) {
            auto [sc, sc_idx] = round_size_class(input_size);
            if (sc.size == 0) {
                // large allocation -- bypass slab/CFL path
                return nullptr;
            }

            Slab* slab = per_cpu_caches[Slab::get_current_cpu_id()];

            void* ptr = slab->allocate(sc_idx);
            if (ptr) return ptr;

            void** dest = slab->push_destination(sc_idx);
            uint32_t got = transfer_caches[sc_idx].RemoveRange(dest, sc.batch_size);

            if (got == 0) {
                got = free_list[sc_idx].allocate_batch(dest, sc.batch_size);
            }
            if (got > 0) {
                slab->commit_push(sc_idx, got);
                return slab->allocate(sc_idx);
            }

            return nullptr;
        }

        void deallocate(void* mem) {
            Span* span = page_heap.span_for(mem);
            if (!span) return;
            auto sc_idx = size_class_to_idx(span->size);
            auto sc = SizeClasses[sc_idx];
            if (sc.size == 0) {
                // large allocation -- return directly to page heap
                return;
            }

            Slab* slab = per_cpu_caches[Slab::get_current_cpu_id()];

            if (slab->deallocate(mem, sc_idx)) return;

            uint32_t n = sc.batch_size;
            void** src = slab->pop_source(sc_idx, n);

            uint32_t inserted = transfer_caches[sc_idx].InsertRange(src, n);
            if (inserted == 0) {
                free_list[sc_idx].deallocate_batch(src, n);
            }

            slab->commit_pop(sc_idx, n);
            slab->deallocate(mem, sc_idx);
        }

    private:
        PageHeap page_heap;
        CentralFreeList free_list[NUM_SIZE_CLASSES];
        TransferCache transfer_caches[NUM_SIZE_CLASSES];
        Slab** per_cpu_caches;

};
#endif