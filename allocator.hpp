#include "central_free_list.hpp"
#include "config.hpp"

/*
Orchestrates the three layers of the memory allocator.

In the middle-end (central free lists + transfer cache),
a size class's central free list should be lazily initialized,
i.e., it should only be created once it is explicitly requested.
*/
struct Allocator {
    Allocator(){
        for (const auto& sc : SizeClasses) {
            CentralFreeList(sc)
        }
    }

    void init_thread_cache() {};
};