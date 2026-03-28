#ifndef SIZE_CLASSES
#define SIZE_CLASSES

#include <cstddef>
#include <cstdint>

struct SizeClassInfo {
    uint32_t size; // max # of bytes this size class can store
    uint8_t pages; // # of pages to allocate at a time
    uint8_t batch_size; // # of objects that can be moved between a cache and a central list/transfer cache in one shot
    uint16_t byte_budget; // determines how many objects get cached per CPU for the size class
};

/*
TODO:
round a given byte size up to the next size class.
*/
static SizeClassInfo round_size_class(size_t in) {
    return SizeClassInfo{};
}

static uint32_t size_class_to_idx(SizeClassInfo sc) {
    
}

#endif