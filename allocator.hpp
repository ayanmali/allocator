#ifndef ALLOCATOR
#define ALLOCATOR
/*
Connects the three layers of the memory allocator.
*/
#include <stdexcept>
#include <sched.h>
#include <sys/mman.h>
#include "central_free_list.hpp"
#include "page_heap.hpp"
#include "size_classes.hpp"
#include "slabs.hpp"
#include "transfer_cache.hpp"

struct Allocator {
    public:
        Allocator() {
            // initialize each CFL
            for (uint32_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
                new(&free_list[i]) CentralFreeList(&page_heap, SizeClasses[i]);
            }
#if defined(__linux__)
            cpu_set_t affinity_mask;
            CPU_ZERO(&affinity_mask);
            sched_getaffinity(0, sizeof(affinity_mask), &affinity_mask);
            
            slab_count = 0;
            for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
                if (CPU_ISSET(cpu, &affinity_mask)) {
                    slab_count = static_cast<uint32_t>(cpu + 1);
                }
            }

#else
            slab_count = static_cast<uint32_t>(std::thread::hardware_concurrency());
#endif
            if (slab_count == 0) slab_count = 1;
            per_cpu_caches = static_cast<Slabs*>(mmap(
                nullptr, 
                static_cast<size_t>(slab_count) * SLAB_STRIDE_BYTES,
                PROT_READ | PROT_WRITE,
                MAP_ANONYMOUS | MAP_PRIVATE,
                -1, 0));
            if (per_cpu_caches == MAP_FAILED) {
                throw std::runtime_error("Failed to mmap per-CPU caches");
            }
        }
        Allocator(Allocator& a) = delete;
        Allocator& operator=(const Allocator& a) = delete;
        virtual ~Allocator() = default;

        void* allocate(size_t input_size) {
            auto [sc, sc_idx] = round_size_class(input_size);
            if (sc.size == 0) {
                // large allocation -- bypass slab/CFL path
                return nullptr;
            }

            void* ptr = slab_pop(per_cpu_caches, slab_count, sc_idx);
            if (ptr) return ptr;

            void* batch[MAX_BATCH_SIZE];
            int32_t got = refill_batch(sc_idx, sc.batch_size, batch);
            if (got == 0) return nullptr;

            // Return one object directly and republish the rest via the
            // existing single-object slab fast path.
            void* result = batch[--got];
            while (got > 0) {
                if (!slab_push(per_cpu_caches, slab_count, sc_idx, batch[got - 1])) {
                    release_batch(sc_idx, batch, got);
                    break;
                }
                --got;
            }
            return result;
        }

        void deallocate(void* mem) {
            Span* span = page_heap.span_for(mem);
            if (!span) return;
            auto sc_idx = size_class_to_idx(span->size);
            auto sc = SizeClasses[sc_idx];
            if (sc.size == 0) {
                // TODO: large allocation -- return directly to page heap
                return;
            }

            if (slab_push(per_cpu_caches, slab_count, sc_idx, mem)) return;

            void* batch[MAX_BATCH_SIZE];
            uint32_t drained = 0;
            while (drained < sc.batch_size) {
                void* ptr = slab_pop(per_cpu_caches, slab_count, sc_idx);
                if (!ptr) break;
                batch[drained++] = ptr;
            }

            if (drained > 0) {
                release_batch(sc_idx, batch, drained);
            }

            if (!slab_push(per_cpu_caches, slab_count, sc_idx, mem)) {
                free_list[sc_idx].deallocate(mem);
            }
        }

    private:
        PageHeap page_heap;
        CentralFreeList free_list[NUM_SIZE_CLASSES];
        TransferCache transfer_caches[NUM_SIZE_CLASSES];
        Slabs* per_cpu_caches;
        uint32_t slab_count;

        uint32_t refill_batch(uint32_t sc_idx, uint32_t count, void** batch) {
            uint32_t got = transfer_caches[sc_idx].RemoveRange(batch, count);
            if (got == 0) {
                got = free_list[sc_idx].allocate_batch(batch, count);
            }
            return got;
        }

        void release_batch(uint32_t sc_idx, void** batch, uint32_t count) {
            uint32_t inserted = transfer_caches[sc_idx].InsertRange(batch, count);
            if (inserted < count) {
                free_list[sc_idx].deallocate_batch(batch + inserted, count - inserted);
            }
        }

};
#endif