#include <cstddef>
#include <cstdio>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static constexpr size_t REQ_THRESHOLD = 128000;

struct Block {
    // Block header
    size_t size;
    bool is_free;
    Block* next;
    std::byte memory[];
};

struct Allocator {
    Block* free_list; // start of heap
    size_t available_bytes;

    static constexpr size_t get_header_size() {
        return sizeof(size_t) + sizeof(bool) + sizeof(Block*);
    }

    void* allocate(size_t size) {
        if (size <= available_bytes) {
            // get a block from the free list
            Block* curr = free_list;
            while (curr) {
                // look for the first free block that contains enough size to satisfy the request
                if (curr->is_free && curr->size >= size) {
                    curr->is_free = false;
                    available_bytes -= curr->size;
                    return curr->memory;
                }
                curr = curr->next;
            }
        }

        // allocate additional memory via sbrk
        if (size < REQ_THRESHOLD) {
            Block* block = (Block*)sbrk(size);
            // add to free list
            Block* curr = free_list;
            while (curr && curr->next) {
                curr = curr->next;
            }
            curr->next = block;
            block->is_free = false;
            block->size = size;
            
            return block + get_header_size();
        }

        // for large requests, allocate via mmap
        // allocate a new block and add it to the list
        Block* curr = free_list;
        while (curr && curr->next) {
            curr = curr->next;
        }

        Block* new_block = (Block*) mmap(
            // TODO: consider MAP_NORESERVE, MAP_HUGETLB, MAP_LOCKED, MAP_POPULATE
            curr->next, size,
            PROT_READ | PROT_WRITE, 
            MAP_ANONYMOUS | MAP_PRIVATE,
             -1, 0);

        curr->next = new_block;
        
        // adjust block metadata
        new_block->is_free = false;
        new_block->size = size;

        return new_block + get_header_size();
    }

    void deallocate(Block* ptr) {
        ptr->is_free = true;
    }

};

int main() {
    Allocator allocator{};
    int* n = static_cast<int*>(allocator.allocate(100));
    return 0;
}