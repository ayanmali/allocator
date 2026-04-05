/*
Central free list -- the middle layer between the page heap (backend)
and thread/CPU caches (frontend).

One CentralFreeList instance exists per size class. When it runs out
of objects it requests a span from the page heap and slices them into
fixed-size objects stored in an unrolled linked list of packed 16-bit
indexes, reducing pointer chasing and improving cache behavior.

When all objects from a span have been returned, the span is released
back to the page heap for coalescing.
Objects are stored in per-span free lists rather than a single global
list, so that releasing a fully-free span back to the page heap is
O(1) -- just unlink the span and return it; no scanning required.
*/

#ifndef CENTRAL_FREE_LIST
#define CENTRAL_FREE_LIST

#include "page_heap.hpp"
#include <cstddef>

/*
Each free object within a span is identified by a 16-bit index:
  index = (addr - span_base) / obj_size

A "batch node" lives in one free object's payload and stores:
  [next_idx: 2B][count: 2B][idx_0 ... idx_N: 2B each]

Each node represents (1 + count) free objects: itself plus the
packed indexes.  This forms an unrolled linked list that packs
multiple free-list entries per cache line.
*/
struct PackedNode {
    uint16_t next_idx;
    uint16_t count;
};

static uint16_t pack_capacity(uint32_t obj_size) {
    if (obj_size < sizeof(PackedNode)) return 0;
    return static_cast<uint16_t>((obj_size - sizeof(PackedNode)) / sizeof(uint16_t));
}

static void* index_to_ptr(uint16_t idx, void* base, uint32_t obj_size) {
    return static_cast<std::byte*>(base) + static_cast<size_t>(idx) * obj_size;
}

static uint16_t ptr_to_index(void* ptr, void* base, uint32_t obj_size) {
    return static_cast<uint16_t>(
        (static_cast<std::byte*>(ptr) - static_cast<std::byte*>(base)) / obj_size);
}

static uint16_t* packed_indexes(PackedNode* node) {
    return reinterpret_cast<uint16_t*>(
        reinterpret_cast<std::byte*>(node) + sizeof(PackedNode));
}

/*
An instance of a CentralFreeList corresponds to one size class.
*/
struct CentralFreeList {
    public:
        CentralFreeList()
            : page_heap(nullptr), span_list(nullptr), current_span(nullptr),
              size(0), num_free(0), pages(0) {}

        CentralFreeList(PageHeap* ph, const SizeClassInfo& sc)
            : page_heap(ph), span_list(nullptr), current_span(nullptr),
              size(sc.size), num_free(0), pages(sc.pages) {}

        ~CentralFreeList() {
            Span* curr = span_list;
            while (curr) {
                Span* next = curr->next;
                curr->next = nullptr;
                curr->prev = nullptr;
                curr->freelist_head = INVALID_INDEX;
                curr->num_free_objects = 0;
                curr->total_objects = 0;
                page_heap->return_span(curr);
                curr = next;
            }
            span_list = nullptr;
            current_span = nullptr;
            num_free = 0;
        }

        void* allocate() {
            if (!current_span || current_span->freelist_head == INVALID_INDEX) {
                current_span = find_non_empty_span();
            }
            if (!current_span) {
                fetch_from_page_heap();
            }
            if (!current_span || current_span->freelist_head == INVALID_INDEX) {
                return nullptr;
            }

            auto* base = reinterpret_cast<std::byte*>(
                current_span->start * PAGE_SIZE);
            auto* node = static_cast<PackedNode*>(
                index_to_ptr(current_span->freelist_head, base, size));

            void* result;
            if (node->count > 0) {
                uint16_t* slots = packed_indexes(node);
                uint16_t idx = slots[--node->count];
                result = index_to_ptr(idx, base, size);
            } else {
                result = node;
                current_span->freelist_head = node->next_idx;
            }
            --current_span->num_free_objects;
            --num_free;
            return result;
        }

        void deallocate(void* ptr) {
            if (!ptr) return;

            Span* span = page_heap->span_for(ptr);
            if (!span) return;

            auto* base = reinterpret_cast<std::byte*>(span->start * PAGE_SIZE);
            uint16_t freed_idx = ptr_to_index(ptr, base, size);

            if (span->freelist_head != INVALID_INDEX) {
                auto* head = static_cast<PackedNode*>(
                    index_to_ptr(span->freelist_head, base, size));
                uint16_t cap = pack_capacity(size);
                if (head->count < cap) {
                    packed_indexes(head)[head->count++] = freed_idx;
                    ++span->num_free_objects;
                    ++num_free;
                    if (span->num_free_objects == span->total_objects)
                        release_to_page_heap(span);
                    return;
                }
            }
            auto* new_node = static_cast<PackedNode*>(ptr);
            new_node->next_idx = span->freelist_head;
            new_node->count = 0;
            span->freelist_head = freed_idx;
            ++span->num_free_objects;
            ++num_free;
            if (span->num_free_objects == span->total_objects)
                release_to_page_heap(span);
        }

        uint32_t get_num_free() const { return num_free; }

        uint32_t allocate_batch(void** ptrs, uint32_t count) {
            uint32_t allocated = 0;
            while (allocated < count) {
                void* p = allocate();
                if (!p) break;
                ptrs[allocated++] = p;
            }
            return allocated;
        }

        void deallocate_batch(void** ptrs, uint32_t count) {
            for (uint32_t i = 0; i < count; ++i) {
                deallocate(ptrs[i]);
            }
        }

    private:
        PageHeap* page_heap;
        Span* span_list;
        Span* current_span; // cached pointer to a span w/ free objects, making common case allocate in O(1)
        uint32_t size; // max # of bytes this size class can store
        uint32_t num_free;
        uint8_t pages; // # of pages associated with this size class

        Span* find_non_empty_span() {
            Span* s = span_list;
            while (s) {
                if (s->freelist_head != INVALID_INDEX) return s;
                s = s->next;
            }
            return nullptr;
        }

        void fetch_from_page_heap() {
            if (size == 0 || pages == 0) return;

            // TODO: check if this is correct
            Span* span = page_heap->allocate_span(pages, size);
            if (!span) return;

            span->next = span_list;
            span->prev = nullptr;
            if (span_list) span_list->prev = span;
            span_list = span;

            auto* base = reinterpret_cast<std::byte*>(span->start * PAGE_SIZE);
            size_t total_bytes = static_cast<size_t>(span->num_pages) * PAGE_SIZE;
            uint32_t count = static_cast<uint32_t>(total_bytes / size);
            assert(count <= UINT16_MAX && "too many objects for 16-bit index");
            uint16_t cap = pack_capacity(size);

            span->total_objects = count;
            span->freelist_head = INVALID_INDEX;
            span->num_free_objects = 0;

            uint32_t i = 0;
            while (i < count) {
                uint16_t node_idx = static_cast<uint16_t>(i);
                auto* node = static_cast<PackedNode*>(
                    index_to_ptr(node_idx, base, size));
                node->next_idx = span->freelist_head;
                ++i;

                uint16_t packed = 0;
                uint16_t* slots = packed_indexes(node);
                while (packed < cap && i < count) {
                    slots[packed++] = static_cast<uint16_t>(i);
                    ++i;
                }
                node->count = packed;
                span->freelist_head = node_idx;
            }
            span->num_free_objects = count;
            num_free += count;

            current_span = span;
        }

        // O(1) span release: unlink from the CFL's span list and return
        // to the page heap. No scanning -- the span's objects are already
        // contained in its own free list, which we simply discard.
        void release_to_page_heap(Span* span) {
            if (current_span == span) {
                current_span = nullptr;
            }

            num_free -= span->num_free_objects;

            if (span->prev) {
                span->prev->next = span->next;
            } else {
                span_list = span->next;
            }
            if (span->next) {
                span->next->prev = span->prev;
            }
            span->next = nullptr;
            span->prev = nullptr;

            span->freelist_head = INVALID_INDEX;
            span->num_free_objects = 0;
            span->total_objects = 0;
            page_heap->return_span(span);
        }
};

#endif
