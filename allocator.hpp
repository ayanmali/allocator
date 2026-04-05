#ifndef ALLOCATOR
#define ALLOCATOR
/*
Connects the three layers of the memory allocator.
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
            // initialize each CFL
            for (auto i = 0; i < NUM_SIZE_CLASSES; ++i) {
                free_list[i] = CentralFreeList(&page_heap, SizeClasses[i]);
            }
            const auto num_cpus = std::thread::hardware_concurrency();
            void* per_cpu_region = mmap(nullptr, sizeof(Slab) * num_cpus, 
            PROT_READ | PROT_WRITE,
            MAP_ANONYMOUS | MAP_PRIVATE,
            -1, 0);
            // split the region into Slabs (one per logical CPU)

        }
        Allocator(Allocator& a) = delete;
        Allocator operator=(Allocator& a) = delete;
        virtual ~Allocator() = default;

        void* allocate(size_t input_size) {
            auto [sc, sc_idx] = round_size_class(input_size);
            if (sc.size == 0) {
                // large allocation -- bypass slab/CFL path
                return nullptr;
            }

            uint32_t begin = Slab::get_begin(sc_idx);
            void* ptr = slab_pop(per_cpu_caches, sc_idx, begin);
            if (ptr) return ptr;

            auto cpu_id = static_cast<uint8_t>(*get_rseq_ptr());
            Slab* slab = per_cpu_caches[cpu_id];
            void** dest = slab->push_destination(sc_idx);
            uint32_t got = transfer_caches[sc_idx].RemoveRange(dest, sc.batch_size);

            if (got == 0) {
                got = free_list[sc_idx].allocate_batch(dest, sc.batch_size);
            }
            if (got > 0) {
                slab->commit_push(sc_idx, got);
                return slab_pop(per_cpu_caches, sc_idx, begin);
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

            if (slab_push(per_cpu_caches, sc_idx, mem)) return;

            auto cpu_id = static_cast<uint8_t>(*get_rseq_ptr());
            Slab* slab = per_cpu_caches[cpu_id];
            uint32_t n = sc.batch_size;
            void** src = slab->pop_source(sc_idx, n);

            uint32_t inserted = transfer_caches[sc_idx].InsertRange(src, n);
            if (inserted == 0) {
                free_list[sc_idx].deallocate_batch(src, n);
            }

            slab->commit_pop(sc_idx, n);
            slab_push(per_cpu_caches, sc_idx, mem);
        }

    private:
        PageHeap page_heap;
        CentralFreeList free_list[NUM_SIZE_CLASSES];
        TransferCache transfer_caches[NUM_SIZE_CLASSES];
        Slab** per_cpu_caches;

};
#endif