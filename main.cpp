#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>

static constexpr size_t REQ_THRESHOLD = 400;

struct Block {
    // Block header
    size_t size;
    bool is_free;
    bool mapped;
    Block* next;
    std::byte memory[];
};

static constexpr size_t header_size() {
    return offsetof(Block, memory);
}

struct Allocator {
    static constexpr size_t INITIAL_CHUNK = 4096;
    Block* free_list; // start of heap
    size_t available_bytes;

    Allocator() {
        void* ok = sbrk(static_cast<intptr_t>(INITIAL_CHUNK));
        // error
        if (ok == (void*)-1) {
            free_list = nullptr;
            available_bytes = 0;
            return;
        }
        free_list = static_cast<Block*>(ok);
        free_list->is_free = true;
        free_list->mapped = false;
        free_list->next = nullptr;
        free_list->size = INITIAL_CHUNK - header_size();
        available_bytes = free_list->size;
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
            void* ok = sbrk(size + header_size());
            if (ok == (void*)-1) {
                return (void*)-1;
            }
            Block* block = static_cast<Block*>(ok);
            // add to free list
            Block* curr = free_list;
            while (curr && curr->next) {
                curr = curr->next;
            }
            curr->next = block;

            block->is_free = false;
            block->size = size;
            block->next = nullptr;
            block->mapped = false;
            
            return block->memory;
        }

        // for large requests, allocate via mmap
        // allocate a new block and add it to the list
        Block* curr = free_list;
        while (curr && curr->next) {
            curr = curr->next;
        }

        void* ok = mmap(
            // TODO: consider MAP_NORESERVE, MAP_HUGETLB, MAP_LOCKED, MAP_POPULATE
            nullptr, size + header_size(),
            PROT_READ | PROT_WRITE, 
            MAP_ANONYMOUS | MAP_PRIVATE,
             -1, 0);
        
        if (ok == MAP_FAILED) {
            return MAP_FAILED;
        }
        Block* new_block = static_cast<Block*>(ok);
        curr->next = new_block;
        
        // adjust block metadata
        new_block->is_free = false;
        new_block->size = size;
        new_block->next = nullptr;
        new_block->mapped = true;

        return new_block->memory;
    }

    /*
    Deallocation involves shrinking the program break (to free memory allocated with sbrk)
    and calling `munmap` on mapped memory.
    */
    template <typename T>
    int8_t deallocate(T* ptr) {
        // ptr point to the start of the Block's memory
        Block* block = reinterpret_cast<Block*>(
            reinterpret_cast<std::byte*>(ptr) - header_size()
        );
        return deallocate(block);
    }

    int8_t deallocate(Block* block) {
        if (!block) {
            return -1;
        }

        if (block->mapped) {
            Block* prev = nullptr;
            Block* curr = free_list;
            while (curr && curr != block) {
                prev = curr;
                curr = curr->next;
            }
            if (!curr) {
                return -1;
            }

            if (prev) {
                prev->next = block->next;
            } else {
                free_list = block->next;
            }

            int ok = munmap(block, header_size() + block->size);
            if (ok == -1) {
                // Roll back the unlink if munmap fails.
                if (prev) {
                    prev->next = block;
                } else {
                    free_list = block;
                }
                return -1;
            }
            return 0;
        }

        // For brk-allocated blocks, keep memory in the allocator and mark free.
        block->is_free = true;
        available_bytes += block->size;
        return 0;
    }

};

int main() {
    Allocator allocator{};
    int* n = (int*) allocator.allocate(100 * sizeof(int));

    // testing
    for (int i = 0; i < 100; ++i) {
        n[i] = i;
    }
    for (int j = 0; j < 100; ++j) {
        std::cout << j << "\n";
    }
    //std::cout << "checking OOB: " << n[100] << "\n";

    uint8_t ok = allocator.deallocate(n);
    if (ok != 0) {
        std::cout << "error deallocating memory";
    }

    // int* n = (int*)(allocator.allocate(100));
    // Block* b = (Block*) (n - header_size());
    // // std::cout << "Is free: " << b->is_free << "\n";
    // // std::cout << "Block size: " << b->size << "\n";
    // allocator.deallocate(b);
    return 0;
}