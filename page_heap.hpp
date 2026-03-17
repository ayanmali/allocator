/*
backend includes a page heap where memory requested from the OS comes from.
Page heap manages spans of pages, which are contiguous sets of pages
one page approx = 8 KB

If the frontend requests from the central list and the central list
does not have a suitable block, the page heap provides a span to the central list
which is then split up into blocks.
E.g. Span = 8 KB page
Size class = 64 B

8 KB / 64 B = 128 objects - populate the free list

Large allocations bypass the frontend and go directly to the backend.
pages_needed = ceil(size / page_size)
E.g. 1 MB / 8 KB = 128 pages - page heap allocates a 128-page span.

Spans are provided on a best-fit basis.
Spans are split accordingly so that any extra pages not used for
allocations are sent to the free lists.
*/

#ifndef PAGE_HEAP
#define PAGE_HEAP
#include "config.hpp"
#include <cassert>
#include <cstdint>
#include <sys/mman.h>

 struct Span {
    uintptr_t start; // page ID
    uintptr_t num_pages;
    uint32_t size_class;
    Span* next;
    Span* prev;

};

// using PageMap = std::unordered_map<uintptr_t, Span*>;

struct PageHeap {
    public:
        PageHeap() {
            
        }
        Span* allocate_span(uint32_t num_pages, uint32_t size_class) {
            if (num_pages > MAX_PAGES) {
                return nullptr;
            }
            /*
            search the corresponding free list
            check each free list, starting with the one whose span page size equals `num_pages`
            if 
            */
            for (int i = num_pages - 1; i < MAX_PAGES + 1; ++i) {
                // check the next free list if the current one does not have any spans
                if (!free_lists[i]) {
                    continue;
                }
                // once a free list w/ a suitable span has been found,
                // remove that span from its list and return it
                Span* start = free_lists[i];
                free_lists[i] = start->next;
                start->next = nullptr;
                // TODO: split the span and add the leftover pages to a new span in the corresponding list
                return start;
            }
            // if no suitable span exists, memory must be requested from the OS

            Span* mem  = (Span*)mmap(
                // TODO: consider MAP_NORESERVE, MAP_HUGETLB, MAP_LOCKED, MAP_POPULATE
                nullptr, PAGE_SIZE * num_pages,
                PROT_READ | PROT_WRITE, 
                MAP_ANONYMOUS | MAP_PRIVATE,
                -1, 0);
            // TODO:
            // mem->start = 
            mem->num_pages = num_pages;
            mem->size_class = size_class;

            return mem;

        }
        
        void deallocate_span(Span* span) {
            auto num_pages = span->num_pages;
            // add the span back to the free list (maintains memory address sort order);
            insert(num_pages, span);
        }

    private:
        //PageMap page_map;
        Span* free_lists[MAX_PAGES + 1];

        bool insert(uintptr_t num_pages, Span* span) {
            Span* curr = free_lists[num_pages - 1];
            if (!curr) {

                return true;
            }
            Span* prev = nullptr;
            while (curr) {
                if (span < curr) {
                    if (prev) {
                        prev->next = span;
                        span->next = curr;
                    }
                    else {
                        free_lists[num_pages - 1] = span;
                        span->next = curr;
                    }
                    return true;
                }
                prev = curr;
                curr = curr->next;
            }
        }
};

#endif