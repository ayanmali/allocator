#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static constexpr size_t REQ_THRESHOLD = 128000;

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
        void* base = sbrk(INITIAL_CHUNK);
        // error
        if (base == (void*)-1) {
            free_list = nullptr;
            available_bytes = 0;
            return;
        }
        free_list = static_cast<Block*>(base);
        free_list->is_free = true;
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
            void* base = sbrk(size + header_size());
            if (base == (void*)-1) {
                return (void*)-1;
            }
            Block* block = static_cast<Block*>(base);
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
            
            return block + header_size();
        }

        // for large requests, allocate via mmap
        // allocate a new block and add it to the list
        Block* curr = free_list;
        while (curr && curr->next) {
            curr = curr->next;
        }

        void* base = mmap(
            // TODO: consider MAP_NORESERVE, MAP_HUGETLB, MAP_LOCKED, MAP_POPULATE
            curr->next, size,
            PROT_READ | PROT_WRITE, 
            MAP_ANONYMOUS | MAP_PRIVATE,
             -1, 0);
        
        if (base == MAP_FAILED) {
            return MAP_FAILED;
        }
        Block* new_block = static_cast<Block*>(base);
        curr->next = new_block;
        available_bytes -= curr->size;
        
        // adjust block metadata
        new_block->is_free = false;
        new_block->size = size - header_size();
        new_block->next = nullptr;
        new_block->mapped = true;

        return new_block + header_size();
    }

    /*
    Deallocation involves shrinking the program break (to free memory allocated with sbrk)
    and calling `munmap` on mapped memory.
    */
    template <typename T>
    uint8_t deallocate(T* ptr) {
        // ptr point to the start of the Block's memory
        Block* block = static_cast<Block*>(ptr - header_size());
        return deallocate(block);
    }

    uint8_t deallocate(Block* block) {
        if (block->mapped) {
            int ok = munmap(block->memory, header_size() + block->size);
            if (ok == -1) {
                return -1;
            }
        } 
        else {
            void* base = sbrk(block->size * -1);
            if (base == (void*)-1) {
                return -1;
            }
        }
        block->is_free = true;
        available_bytes += block->size;
        return 0;
    }

};

int main() {
    Allocator allocator{};
    int* n = (int*)(allocator.allocate(100));
    Block* b = (Block*) (n - header_size());
    // std::cout << "Is free: " << b->is_free << "\n";
    // std::cout << "Block size: " << b->size << "\n";
    allocator.deallocate(b);
    return 0;
}