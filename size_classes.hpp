#ifndef SIZE_CLASSES
#define SIZE_CLASSES

#include <cstddef>
#include <cstdint>

struct SizeClassInfo {
    uint32_t size; // max # of bytes this size class can store
    uint8_t pages; // # of pages to allocate at a time
    uint8_t num_to_move; // # of objects that can be moved between a thread list and a central list in one shot
};

#endif