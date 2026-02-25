#include <cstddef>
#include <cstdio>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static constexpr size_t MMAP_THRESHOLD = 128000;

struct Block {
    size_t size;
    bool is_free;
    Block* next;
};

struct Allocator {
    Block* free_list; // start of heap
    size_t available_bytes;

    void* allocate(size_t size) {
        if (size < MMAP_THRESHOLD && size <= available_bytes) {
            // get a block from the free list
            Block* curr = free_list;
            while (curr) {
                // look for the first free block that contains enough size to satisfy the request
                if (curr->is_free && curr->size >= size) {
                    curr->is_free = false;
                    available_bytes -= curr->size;
                    return (void*)(curr + 1);
                }
                curr = curr->next;
            }
        }
        void* mem = mmap(
            // TODO: consider MAP_NORESERVE, MAP_HUGETLB, MAP_LOCKED, MAP_POPULATE
            nullptr, size, 
            PROT_READ | PROT_WRITE, 
            MAP_ANONYMOUS | MAP_PRIVATE,
             -1, 0);
        return mem;
    }

    void deallocate_block(Block* ptr) {
        ptr->is_free = true;
    }

    template <typename T>
    void deallocate_large(T* ptr) {
        munmap(void *addr, size_t len);
    }
};

int main() {
    Allocator allocator{};
    int* n = static_cast<int*>(allocator.allocate(100));
    allocator.deallocate(n, 100);
    return 0;
}