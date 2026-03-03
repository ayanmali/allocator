#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>

static constexpr size_t MMAP_THRESHOLD = 400;

struct Block {
    // Block header
    size_t size; // size of user memory
    bool is_free; // true if the block exists in the free list, false otherwise
    bool mapped; // true if the memory was allocated with mmap, false if it was allocated with sbrk
    Block* next; // linked list of blocks
    std::byte memory[]; // user memory
};

static constexpr size_t header_size() {
    return offsetof(Block, memory);
}

struct Allocator {
    public:
        Allocator() {
            Block* start_block = create_block(
                INITIAL_CHUNK, 
                true,
                false,
                nullptr);
            if (!start_block) {
                free_list = nullptr;
                available_bytes = 0;
                return;
            }
           
            free_list = start_block;
            available_bytes = free_list->size;
        }

        void* allocate(const size_t size) {
            Block* start = free_list;

            // traverse the free list to find a free block that contains enough size to satisfy the request
            if (size <= available_bytes) {
                Block* prev = nullptr;
                Block* curr = free_list;
                while (curr) {
                    // look for the first free block that contains enough size to satisfy the request
                    if (curr->is_free && curr->size >= size) {
                        // pop the block off the free list
                        if (prev) {
                            prev->next = curr->next;
                        } else {
                            free_list = curr->next;
                        }
                        curr->next = nullptr;
                        curr->is_free = false;
                        available_bytes -= curr->size;
                        return curr->memory;
                    }
                    prev = curr;
                    curr = curr->next;
                }
            }

            // for large requests, allocate via mmap. For small allocations, shift the program break instead
            // allocate a new block and add it to the list
            Block* new_block = create_block(
                size, 
                false, 
                size >= MMAP_THRESHOLD, 
                nullptr);

            if (!new_block) {
                return (void*)-1;
            }

            return new_block->memory;
        }

        // Returns a block to the free list for reuse, but does not free the memory.
        // TODO: implement coalescing
        template <typename T>
        bool return_back(T* ptr) {
            // ptr points to the start of the Block's memory
            Block* block = reinterpret_cast<Block*>(
                reinterpret_cast<std::byte*>(ptr) - header_size());
            if (!block) {
                return false;
            }

            // add the block to the start of the free list
            Block* start = free_list;
            block->next = start;
            block->is_free = true;
            free_list = block;
            available_bytes += block->size;
            return true;
        }

        /*
        Deallocation involves shrinking the program break (to free memory allocated with sbrk)
        or calling `munmap` on mapped memory.
        */
        template <typename T>
        bool deallocate(T* ptr) {
            // ptr point to the start of the Block's memory
            Block* block = reinterpret_cast<Block*>(
                reinterpret_cast<std::byte*>(ptr) - header_size());
            return deallocate(block);
        }

        /*
        The block may currently be in the free list, in which case we need to unlink it.
        In any case, memory must be cleaned up
        */
        bool deallocate(Block* block) {
            if (!block) {
                return false;
            }

            // if the block is free, unlink the block from the free list
            Block* prev;
            if (block->is_free) {
                prev = nullptr;
                Block* curr = free_list;
                while (curr && curr != block) {
                    prev = curr;
                    curr = curr->next;
                }
                if (!curr) {
                    return false;
                }
                if (prev) {
                    prev->next = block->next;
                } else {
                    free_list = block->next;
                }
            }

            if (block->mapped) {
                int ok = munmap(block, header_size() + block->size);
                if (ok == -1) {
                    // Roll back the unlink if munmap fails.
                    if (prev) {
                        prev->next = block;
                    } else {
                        free_list = block;
                    }
                    return false;
                }
                return true;
            }

            // For brk-allocated blocks, can shrink the program break if the block is the last block in the heap
            int ok = brk(block);
            if (ok == -1) {
                return false;
            }
    
            available_bytes += block->size;
            return true;
        }

    private:
        static constexpr size_t INITIAL_CHUNK = 4096;
        Block* free_list; // start of heap
        size_t available_bytes;

        Block* create_block(size_t size, bool is_free, bool mapped, Block* next) {
            void* ok = mapped ? 
            mmap(
                // TODO: consider MAP_NORESERVE, MAP_HUGETLB, MAP_LOCKED, MAP_POPULATE
                nullptr, size + header_size(),
                PROT_READ | PROT_WRITE, 
                MAP_ANONYMOUS | MAP_PRIVATE,
                -1, 0)
            :
            sbrk(static_cast<intptr_t>(size + header_size()));

            // error
            if (ok == (void*)-1) {
                return nullptr;
            }
            Block* block = static_cast<Block*>(ok);
            block->size = size;
            block->is_free = is_free;
            block->mapped = mapped;
            block->next = next;
            return block;
        }

        Block* create_block(size_t size, bool is_free, Block* next) {
            bool mapped = size > MMAP_THRESHOLD;
            void* ok = mapped ? 
            mmap(
                // TODO: consider MAP_NORESERVE, MAP_HUGETLB, MAP_LOCKED, MAP_POPULATE
                nullptr, size + header_size(),
                PROT_READ | PROT_WRITE, 
                MAP_ANONYMOUS | MAP_PRIVATE,
                -1, 0)
            :
            sbrk(static_cast<intptr_t>(size + header_size()));

            // error
            if (ok == (void*)-1) {
                return nullptr;
            }
            Block* block = static_cast<Block*>(ok);
            block->size = size;
            block->is_free = is_free;
            block->mapped = mapped;
            block->next = next;
            return block;
        }

        // coalesce adjacent free blocks together into one combined block
        bool coalesce(Block* prev) {
            // if `block` and `next` are in the list, then they must be free
            Block* block = prev->next;
            Block* next = block->next;
            if (block && next) {
                // merge them by adding a new block
                Block* new_block = create_block(block->size + next->size, true, next->next);
                // delete the block->next pointer
            }
            return true;
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

    char* s = (char*) allocator.allocate(50 * sizeof(char));
    std::string idk = "abcdefghijklmnopqrstuvwxyz";
    for (int k = 0; k < 50; ++k) {
        s[k] = idk[k % idk.size()];
    }

    for (int m = 0; m < 50; ++m) {
        std::cout << s[m] << "\n";
    }

    int8_t ok = allocator.deallocate(n);
    if (ok != 0) {
        std::cout << "error deallocating memory - int";
    }

    ok = allocator.deallocate(s);
    if (ok != 0) {
        std::cout << "error deallocating memory - char";
    }

    return 0;
}