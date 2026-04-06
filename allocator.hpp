#ifndef ALLOCATOR
#define ALLOCATOR
/*
Connects the three layers of the memory allocator.
*/
#include <stdexcept>
#include <thread>
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
            const size_t num_cpus = static_cast<uint32_t>(CPU_COUNT(&affinity_mask));
#else
            const size_t num_cpus = std::thread::hardware_concurrency();
#endif
            per_cpu_caches = static_cast<Slabs*>(mmap(nullptr, num_cpus * SLAB_STRIDE_BYTES,
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

            void* ptr = slab_pop(per_cpu_caches, sc_idx);
            if (ptr) return ptr;

            uint32_t cpu_id = current_cpu_id();
            Slabs* slabs = get_slabs(per_cpu_caches, cpu_id);
            void** dest = slabs->push_destination(sc_idx);
            uint32_t got = transfer_caches[sc_idx].RemoveRange(dest, sc.batch_size);

            if (got == 0) {
                got = free_list[sc_idx].allocate_batch(dest, sc.batch_size);
            }
            if (got > 0) {
                slabs->commit_push(sc_idx, got);
                return slab_pop(per_cpu_caches, sc_idx);
            }

            return nullptr;
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

            if (slab_push(per_cpu_caches, sc_idx, mem)) return;

            uint32_t cpu_id = current_cpu_id();
            Slabs* slabs = get_slabs(per_cpu_caches, cpu_id);
            uint32_t n = sc.batch_size;
            void** src = slabs->pop_source(sc_idx, n);

            uint32_t inserted = transfer_caches[sc_idx].InsertRange(src, n);
            if (inserted == 0) {
                free_list[sc_idx].deallocate_batch(src, n);
            }

            slabs->commit_pop(sc_idx, n);
            slab_push(per_cpu_caches, sc_idx, mem);
        }

    private:
        PageHeap page_heap;
        CentralFreeList free_list[NUM_SIZE_CLASSES];
        TransferCache transfer_caches[NUM_SIZE_CLASSES];
        Slabs* per_cpu_caches;

};
#endif