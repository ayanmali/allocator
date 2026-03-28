#ifndef SLABS
#define SLABS
#include "config.hpp"
#include "size_classes.hpp"

/*
One slab per each CPU
Each slab stores 32 bit headers contiguously (one per size class) that contain indexes into the size class array of pointers
After the last header comes the first pointer array for the first size class.
Each pointer array's capacity should be a multiple of the corresponding size class's batch size

*/

struct SlabHeader {
    uint16_t current; // points to next valid slot
    uint16_t end; // one past the last valid slot
};

/*
N separate slabs, one for each logical CPU
Each slab contains an array of pointers, which is packed to contain pointers to objects across all size classes 
*/
struct Slab {
    public:
        Slab(uint32_t slab_base) {
            // set the current and end indices for each header
            for (int i = 0; i < NUM_SIZE_CLASSES; ++i) {
                headers[i].current = get_begin(slab_base, i) + 1;
                headers[i].end = SIZE_CLASS_OFFSETS[i];
                ++i;     
            }
        }

        /*
        Popping from the stack
        */
        void* allocate(Slab& slab, size_t size) {
            auto cpu = get_current_cpu_id();
            auto size_class = round_size_class(size);
            auto size_class_idx = size_class_to_idx(size_class);
            auto begin = get_begin(slab_base, size_class_idx);
            if (current > begin) {
                current--;
                // return the object at the current index
            }
            return nullptr;

        }
        
        /*
        Pushing onto the stack
        */
        bool deallocate(Slab& slab, void* mem) {
            auto cpu = get_current_cpu_id();
            if (current < end) {
                current++;
                return true;
            }
            return false;
        }

        static uint32_t get_begin(uint32_t slab_base, uint32_t size_class_idx) {
            return slab_base + SIZE_CLASS_OFFSETS[size_class_idx];
        }

        /*
        Determines which slab to access (i.e. the index into the slabs array)
        */
        static int get_current_cpu_id() {
        }

    private:
        SlabHeader headers[NUM_SIZE_CLASSES];
        void* pointers[]; // array of pointers across every size class

};
#endif