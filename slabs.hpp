#ifndef SLABS
#define SLABS
#include "config.hpp"
#include "size_classes.hpp"
#include <sched.h>

/*
One slab per each CPU
Each slab stores 32 bit headers contiguously (one per size class) that contain indexes into the size class array of pointers
After the last header comes the first pointer array for the first size class.
Each pointer array's capacity should be a multiple of the corresponding size class's batch size

*/

extern "C" {
    extern ptrdiff_t __rseq_offset;
    extern unsigned int __rseq_size;
}

struct SlabHeader {
    uint16_t current; // points to next valid slot
    uint16_t end; // one past the last valid slot
};

/*
Each slab represents a cache corresponding to one logical CPU.
Each slab contains an array of pointers, which is packed to contain pointers to objects across all size classes 
*/
struct Slab {
    public:
        Slab() {
            // set the current and end indices for each header
            for (uint32_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
                headers[i].current = SIZE_CLASS_OFFSETS[i];
                headers[i].end = SIZE_CLASS_OFFSETS[i + 1];
            }
        }

        /*
        Popping from the stack
        TODO: implement prefetching?
        */
        void* allocate(uint32_t size_class_idx) {
            auto& hdr = headers[size_class_idx];
            auto begin = SIZE_CLASS_OFFSETS[size_class_idx];
            if (hdr.current > SIZE_CLASS_OFFSETS[size_class_idx]) {
                auto idx = hdr.current - 1;
                auto ptr = pointers[idx];
                hdr.current = idx; // rseq commit operation
                return ptr;
            }
            return nullptr;
        }
        
        /*
        Pushing onto the stack
        */
        bool deallocate(void* mem, uint32_t size_class_idx) {
            auto& hdr = headers[size_class_idx];
            if (hdr.current < hdr.end) {
                auto idx = hdr.current + 1;
                // attempt to push onto stack; if full, return false
                pointers[idx] = mem;
                hdr.current++;
                return true;
            }
            return false;
        }

        void** push_destination(uint32_t size_class_idx) {
            return &pointers[headers[size_class_idx].current];
        }

        void commit_push(uint32_t size_class_idx, uint32_t n) {
            headers[size_class_idx].current += n;
        }

        void** pop_source(uint32_t size_class_idx, uint32_t n) {
            return &pointers[headers[size_class_idx].current - n];
        }

        void commit_pop(uint32_t size_class_idx, uint32_t n) {
            headers[size_class_idx].current -= n;
        }

        uint32_t available(uint32_t size_class_idx) {
            return headers[size_class_idx].current - SIZE_CLASS_OFFSETS[size_class_idx];
        }

        uint32_t remaining(uint32_t size_class_idx) {
            return headers[size_class_idx].end - headers[size_class_idx].current;
        }

        static uint32_t get_begin(uint32_t size_class_idx) {
            return SIZE_CLASS_OFFSETS[size_class_idx];
        }

        /*
        Determines which slab to access (i.e. the index into the slabs array)
        */
        static int get_current_cpu_id() {
            if (__builtin_expect(__rseq_size >= 4, 1)) {

                auto* rs = reinterpret_cast<unsigned*>(
                    reinterpret_cast<char*>(__builtin_thread_pointer()) + __rseq_offset);
                return static_cast<int>(*rs);  // cpu_id is the first 32-bit field
            }
            return sched_getcpu(); // fallback
        }

    private:
        SlabHeader headers[NUM_SIZE_CLASSES];
        void* pointers[]; // array of pointers across every size class

};
#endif