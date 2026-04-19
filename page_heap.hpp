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
#include <mutex>
#include <unordered_map>
#include <sys/mman.h>

static constexpr uint16_t INVALID_INDEX = 0xFFFF;

struct Span {
    uintptr_t start;      // page ID (address / PAGE_SIZE)
    uintptr_t num_pages;
    Span* next;
    Span* prev;
    uint32_t size; // the size of the objects (bytes) stored in the Span
    uint32_t total_objects;     // objects carved from this span (set by CentralFreeList)
    uint32_t num_free_objects;  // free objects currently linked into the span's free list
    uint32_t next_uninitialized = 0; // first object that has not been carved yet
    uint16_t freelist_head = INVALID_INDEX; // index of first packed batch node
    bool is_free;
};

struct PageHeap {
    public:
        PageHeap() : free_lists() {}

        ~PageHeap() {
            for (uint32_t i = 0; i <= MAX_PAGES; ++i) {
                Span* curr = free_lists[i];
                while (curr) {
                    Span* n = curr->next;
                    munmap(reinterpret_cast<void*>(curr->start * PAGE_SIZE),
                           static_cast<size_t>(curr->num_pages) * PAGE_SIZE);
                    delete curr;
                    curr = n;
                }
                free_lists[i] = nullptr;
            }
            page_map.clear();
        }

        Span* allocate_span(uint32_t num_pages, uint32_t size) {
            if (num_pages == 0 || num_pages > MAX_PAGES) {
                return nullptr;
            }
            const std::lock_guard<std::mutex> lock(mu);

            /*
            Search free lists starting at the requested page count.
            Within each list, pick the span whose size is closest to
            the requested one (reuse-friendly for the central free list).
            */
            for (uint32_t i = num_pages; i <= MAX_PAGES; ++i) {
                if (!free_lists[i]) {
                    continue;
                }

                Span* best_span = nullptr;
                uint32_t best_diff = UINT32_MAX;
                Span* curr = free_lists[i];
                while (curr) {
                    uint32_t diff = (curr->size > size)
                        ? curr->size - size
                        : size - curr->size;
                    if (diff < best_diff) {
                        best_diff = diff;
                        best_span = curr;
                    }
                    if (diff == 0) break;
                    curr = curr->next;
                }

                if (!best_span) continue;

                remove_from_free_list(best_span);
                best_span->is_free = false;
                best_span->size = size;

                if (best_span->num_pages > num_pages) {
                    split(best_span, num_pages);
                }
                return best_span;
            }

            // No suitable span found; request memory from the OS
            // TODO: consider MAP_NORESERVE, MAP_HUGETLB, MAP_LOCKED, MAP_POPULATE
            void* raw = mmap(
                nullptr, static_cast<size_t>(PAGE_SIZE) * num_pages,
                PROT_READ | PROT_WRITE,
                MAP_ANONYMOUS | MAP_PRIVATE,
                -1, 0);
            if (raw == MAP_FAILED) {
                return nullptr;
            }

            Span* span = new Span{};
            span->start = reinterpret_cast<uintptr_t>(raw) / PAGE_SIZE;
            span->num_pages = num_pages;
            span->size = size;
            span->is_free = false;
            span->next = nullptr;
            span->prev = nullptr;

            register_span(span); // update page map
            return span;
        }

        void return_span(Span* span) {
            if (!span) return;
            const std::lock_guard<std::mutex> lock(mu);
            span->is_free = true;
            span->size = 0;

            coalesce(span);

            // If coalescing produced a span larger than MAX_PAGES, carve
            // off MAX_PAGES-sized chunks from the front until it fits.
            while (span->num_pages > MAX_PAGES) {
                Span* front = new Span{};
                front->start = span->start;
                front->num_pages = MAX_PAGES;
                front->size = 0;
                front->is_free = true;
                front->next = nullptr;
                front->prev = nullptr;

                unregister_span(span);
                span->start += MAX_PAGES;
                span->num_pages -= MAX_PAGES;
                register_span(span);
                register_span(front);

                insert(MAX_PAGES, front);
            }

            insert(span->num_pages, span);
        }

        Span* span_for(void* ptr) {
            const std::lock_guard<std::mutex> lock(mu);
            uintptr_t page_id = reinterpret_cast<uintptr_t>(ptr) / PAGE_SIZE;
            return lookup(page_id);
        }

        bool deallocate_span(Span* span) {
            if (!span) return false;
            const std::lock_guard<std::mutex> lock(mu);

            if (span->is_free) {
                remove_from_free_list(span);
            }

            void* addr = reinterpret_cast<void*>(span->start * PAGE_SIZE);
            unregister_span(span);
            int result = munmap(addr,
                                static_cast<size_t>(span->num_pages) * PAGE_SIZE);
            delete span;
            return result == 0;
        }

    private:
        Span* free_lists[MAX_PAGES + 1];
        // TODO: replace w/ radix tree or cache friendly hash table
        std::unordered_map<uintptr_t, Span*> page_map;
        std::mutex mu;

        // ---- page map helpers ----

        // Map every page in the span so that span_for() can resolve
        // any address within the span, not just the boundaries.
        void register_span(Span* span) {
            for (uintptr_t p = 0; p < span->num_pages; ++p) {
                page_map[span->start + p] = span;
            }
        }

        void unregister_span(Span* span) {
            for (uintptr_t p = 0; p < span->num_pages; ++p) {
                page_map.erase(span->start + p);
            }
        }

        Span* lookup(uintptr_t page_id) {
            auto it = page_map.find(page_id);
            return (it != page_map.end()) ? it->second : nullptr;
        }

        // ---- coalescing ----

        void coalesce(Span* span) {
            // Merge with the left neighbor (the span whose last page
            // is immediately before this span's first page).
            if (span->start > 0) {
                Span* left = lookup(span->start - 1);
                if (left && left->is_free && left != span) {
                    remove_from_free_list(left);
                    unregister_span(left);
                    unregister_span(span);
                    span->start = left->start;
                    span->num_pages += left->num_pages;
                    register_span(span);
                    delete left;
                }
            }

            // Merge with the right neighbor (the span whose first page
            // is immediately after this span's last page).
            Span* right = lookup(span->start + span->num_pages);
            if (right && right->is_free && right != span) {
                remove_from_free_list(right);
                unregister_span(right);
                unregister_span(span);
                span->num_pages += right->num_pages;
                register_span(span);
                delete right;
            }
        }

        // ---- splitting ----

        // Keeps the first `num_pages` in `span`; the remainder becomes
        // a new free span that is inserted into the appropriate free list.
        void split(Span* span, uint32_t num_pages) {
            assert(span->num_pages > num_pages);

            Span* remainder = new Span{};
            remainder->start = span->start + num_pages;
            remainder->num_pages = span->num_pages - num_pages;
            remainder->size= 0;
            remainder->is_free = true;
            remainder->next = nullptr;
            remainder->prev = nullptr;

            unregister_span(span);
            span->num_pages = num_pages;
            register_span(span);
            register_span(remainder);

            if (remainder->num_pages <= MAX_PAGES) {
                insert(remainder->num_pages, remainder);
            }
        }

        // ---- free-list helpers ----

        // O(1) removal using the prev pointer.
        void remove_from_free_list(Span* span) {
            uintptr_t idx = span->num_pages;
            if (idx > MAX_PAGES) return;

            if (span->prev) {
                span->prev->next = span->next;
            } else {
                free_lists[idx] = span->next;
            }
            if (span->next) {
                span->next->prev = span->prev;
            }

            span->next = nullptr;
            span->prev = nullptr;
            span->is_free = false;
        }

        // Sorted insertion by page ID (start) so that address-ordered
        // traversal is possible.
        void insert(uintptr_t num_pages, Span* span) {
            if (num_pages > MAX_PAGES) return;

            span->is_free = true;
            span->next = nullptr;
            span->prev = nullptr;

            Span*& head = free_lists[num_pages];
            if (!head) {
                head = span;
                return;
            }

            Span* prev = nullptr;
            Span* curr = head;
            while (curr) {
                if (span->start < curr->start) {
                    // insert
                    span->next = curr;
                    span->prev = prev;
                    curr->prev = span;
                    if (prev) {
                        prev->next = span;
                    } else {
                        head = span;
                    }
                    return;
                }
                prev = curr;
                curr = curr->next;
            }
            prev->next = span;
            span->prev = prev;
        }
};

#endif
