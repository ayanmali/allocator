#ifndef ALLOCATOR
#define ALLOCATOR
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cassert>
#include <iostream>

static constexpr size_t MMAP_THRESHOLD = 400;

struct Block {
    // Block header
    size_t size; // size of user memory
    bool is_free; // true if the block exists in the free list, false otherwise
    bool mapped; // true if the memory was allocated with mmap, false if it was allocated with sbrk
    Block* next; // linked list of blocks
    std::byte memory[]; // user memory

    // footer (boundary tag) implicitly placed at the end of the block
};

struct Footer {
    size_t size;
    bool mapped;
};

static constexpr size_t header_size() {
    return offsetof(Block, memory);
}

static constexpr size_t footer_size() {
    return sizeof(Footer);
}

static constexpr size_t overhead_size() {
    return header_size() + footer_size();
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

        virtual ~Allocator() {
            // clean up
            while (free_list) {
                deallocate(free_list);
            }
        }

        void* allocate(const size_t size) {
            // traverse the free list to find a free block that contains enough size to satisfy the request
            if (size <= available_bytes) {
                Block* prev;
                Block* curr;
                // start searching from where the last search ended
                if (prev_search_end && prev_search_end->next) {
                    prev = prev_search_end;
                    curr = prev_search_end->next;
                }
                else {
                    // start searching from start of list
                    prev = nullptr;
                    curr = free_list;
                }
                while (curr) {
                    // look for the first free block that contains enough size to satisfy the request
                    if (curr->is_free && curr->size >= size) {
                        // curr is divided into a smaller block to match the requested size;
                        // leftover space becomes its own block next in the list
                        split(curr, size);
                        // pop the block off the free list
                        if (prev) {
                            prev->next = curr->next;
                            prev_search_end = prev;
                        } else {
                            free_list = curr->next;
                            prev_search_end = nullptr;
                        }
                        curr->next = nullptr;
                        curr->is_free = false;
                        available_bytes -= curr->size;
#ifndef NDEBUG
                        assert_header_footer_match(curr);
#endif
                        return curr->memory;
                    }

                    // if at the end of the list, wrap around to the start
                    if (prev_search_end && !curr->next) {
                        prev = nullptr;
                        curr = free_list;
                        continue;
                    }
                    prev = curr;
                    curr = curr->next;
                    if (prev_search_end && curr == prev_search_end->next) {
                        break;
                    }
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

#ifndef NDEBUG
            assert_header_footer_match(new_block);
#endif
            return new_block->memory;
        }

        // Returns a block to the free list for reuse, but does not free the memory.
        template <typename T>
        bool return_back(T* ptr) {
            // ptr points to the start of the Block's memory
            Block* block = reinterpret_cast<Block*>(
                reinterpret_cast<std::byte*>(ptr) - header_size());
            return return_back(block);
        }

        bool return_back(Block* block) {
            if (!block || block->is_free) {
                return false;
            }

            block->is_free = true;
            block->next = nullptr;
            available_bytes += block->size;

            Block* merged = coalesce_around(block);
            if (merged == block) {
                insert_free(block);
            }

#ifndef NDEBUG
            assert_header_footer_match(merged);
#endif

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
            if (block->is_free) {
                if (!unlink_free(block)) {
                    return false;
                }
                available_bytes -= block->size;
            }

            if (block->mapped) {
                int ok = munmap(block, block_span(block));
                return ok != -1;
            }

            // For brk-allocated blocks, only shrink if this is the heap tail.
            if (block_end(block) != brk_heap_end) {
                #ifndef NDEBUG
                std::cout << "brk-allocated block not at heap end; returning to free list instead\n";
                #endif
                
                return_back(block);
                return true;
            }
            int result = brk(reinterpret_cast<void*>(block));
            if (result == -1) {
                return false;
            }
            brk_heap_end = reinterpret_cast<std::byte*>(block);
            return true;
        }

    private:
        static constexpr size_t INITIAL_CHUNK = 4096;
        Block* free_list; // start of heap
        Block* prev_search_end; // pointer to the block where the previous allocation ended
        size_t available_bytes;
        std::byte* brk_heap_start = nullptr;
        std::byte* brk_heap_end = nullptr;

        static size_t block_span(const Block* block) {
            return overhead_size() + block->size;
        }

        static std::byte* block_end(Block* block) {
            return reinterpret_cast<std::byte*>(block) + block_span(block);
        }

        static Footer* footer_of(Block* block) {
            //return reinterpret_cast<Footer*>(block_end(block) - footer_size());
            return reinterpret_cast<Footer*>(block->memory + block->size);
        }

        static void write_footer(Block* block) {
            Footer* footer = footer_of(block);
            footer->size = block->size;
            footer->mapped = block->mapped;
        }

#ifndef NDEBUG
        static void assert_header_footer_match(Block* block) {
            Footer* footer = footer_of(block);
            assert(footer->size == block->size);
            assert(footer->mapped == block->mapped);
        }
#endif

        Block* next_phys(Block* block) const {
            if (!block || block->mapped) {
                return nullptr;
            }
            std::byte* next_addr = block_end(block);
            if (!brk_heap_end || next_addr >= brk_heap_end) {
                return nullptr;
            }
            return reinterpret_cast<Block*>(next_addr);
        }

        Block* prev_phys(Block* block) const {
            if (!block || block->mapped || !brk_heap_start) {
                return nullptr;
            }
            std::byte* block_addr = reinterpret_cast<std::byte*>(block);
            if (block_addr <= brk_heap_start + header_size()) {
                return nullptr;
            }

            Footer* prev_footer = reinterpret_cast<Footer*>(block_addr - footer_size());
            size_t prev_size = prev_footer->size;
            std::byte* prev_addr = block_addr - (overhead_size() + prev_size);
            if (prev_addr < brk_heap_start) {
                return nullptr;
            }

            Block* prev = reinterpret_cast<Block*>(prev_addr);
            return prev->mapped == prev_footer->mapped ? prev : nullptr;
        }

        // Inserts the block at the start of the free list
        void insert_free(Block* block) {
            block->next = free_list;
            free_list = block;
        }

        bool unlink_free(Block* block) {
            Block* prev = nullptr;
            Block* curr = free_list;
            while (curr && curr != block) {
                prev = curr;
                curr = curr->next;
            }
            if (!curr) {
                return false;
            }
            if (prev) {
                prev->next = curr->next;
                if (curr == prev_search_end) {
                    prev_search_end = prev;
                }
            } 
            else {
                free_list = curr->next;
            }

            if (prev && curr == prev_search_end) {
                prev_search_end = prev;
            }

            curr->is_free = false;
            curr->next = nullptr;
            return true;
        }

        Block* create_block(size_t size, bool is_free, bool mapped, Block* next) {
            void* ok = mapped ? 
            mmap(
                // TODO: consider MAP_NORESERVE, MAP_HUGETLB, MAP_LOCKED, MAP_POPULATE
                nullptr, size + overhead_size(),
                PROT_READ | PROT_WRITE, 
                MAP_ANONYMOUS | MAP_PRIVATE,
                -1, 0)
            :
            sbrk(static_cast<intptr_t>(size + overhead_size()));

            // error
            if (ok == (void*)-1) {
                return nullptr;
            }
            Block* block = static_cast<Block*>(ok);
            block->size = size;
            block->is_free = is_free;
            block->mapped = mapped;
            block->next = next;
            write_footer(block);

            if (!mapped) {
                if (!brk_heap_start) {
                    brk_heap_start = reinterpret_cast<std::byte*>(block);
                }
                brk_heap_end = block_end(block);
            }

#ifndef NDEBUG
            assert_header_footer_match(block);
#endif
            return block;
        }

        Block* coalesce_around(Block* block) {
            if (!block || block->mapped) {
                return block;
            }

            Block* owner = block;
            Block* prev = prev_phys(block);
            if (prev && prev->is_free && prev->mapped == block->mapped) {
                prev->size += overhead_size() + block->size;
                write_footer(prev);
                available_bytes += overhead_size();
                owner = prev;
            }

            // look for more blocks to coalesce
            while (true) {
                Block* next = next_phys(owner);
                if (!next || !next->is_free || next->mapped != owner->mapped) {
                    break;
                }
                unlink_free(next);
                owner->size += overhead_size() + next->size;
                write_footer(owner);
                available_bytes += overhead_size();
            }

#ifndef NDEBUG
            assert_header_footer_match(owner);
#endif
            return owner;
        }

        /*
        splits a block into two smaller ones.
        The original block will have a size of `partition`.
        The second, new block will have a size of `block->size - partition`
        */
        bool split(Block* block, size_t partition) {
            if (!block || partition > block->size) {
                return false;
            }

            // Need enough room for a second block header/footer and at least 1 byte payload.
            if (block->size < partition + overhead_size() + 1) {
                return false;
            }

            const size_t original_size = block->size;
            const bool original_mapped = block->mapped;
            Block* old_next = block->next;

            // Resize the front block to the requested partition.
            block->size = partition;
            write_footer(block);

            // Place the remainder block immediately after the resized front block.
            std::byte* remainder_addr = block_end(block);
            Block* remainder = reinterpret_cast<Block*>(remainder_addr);
            remainder->size = original_size - partition - overhead_size();
            remainder->is_free = true;
            remainder->mapped = original_mapped;
            remainder->next = old_next;
            write_footer(remainder);

            // Keep list continuity through the original node (caller may unlink front block).
            block->next = remainder;
            available_bytes -= overhead_size();

#ifndef NDEBUG
            assert_header_footer_match(block);
            assert_header_footer_match(remainder);
#endif
            return true;
        }

};
#endif