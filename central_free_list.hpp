/*
Central free list -- the middle layer between the page heap (backend)
and thread/CPU caches (frontend).

One CentralFreeList instance exists per size class. When it runs out
of objects it requests a span from the page heap and slices them into
fixed-size objects using an embedded free list (each free object stores
a next-pointer in its own payload bytes).

Objects are stored in per-span free lists rather than a single global
list, so that releasing a fully-free span back to the page heap is
O(1) -- just unlink the span and return it; no scanning required.
*/

#ifndef CENTRAL_FREE_LIST
#define CENTRAL_FREE_LIST

#include "page_heap.hpp"
#include <cstddef>

struct FreeObject {
    FreeObject* next;
};

/*
An instance of a CentralFreeList corresponds to one size class.
*/
struct CentralFreeList {
    public:
        CentralFreeList()
            : sc_info(), num_free(0), span_list(nullptr),
              current_span(nullptr), page_heap(nullptr) {}

        CentralFreeList(SizeClassInfo& sc, PageHeap* ph)
            : sc_info(sc), num_free(0), span_list(nullptr),
              current_span(nullptr), page_heap(ph) {}

        ~CentralFreeList() {
            Span* curr = span_list;
            while (curr) {
                Span* next = curr->next;
                curr->next = nullptr;
                curr->prev = nullptr;
                curr->free_objects = nullptr;
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
            if (!current_span || !current_span->free_objects) {
                current_span = find_non_empty_span();
            }
            if (!current_span) {
                fetch_from_page_heap();
            }
            if (!current_span || !current_span->free_objects) {
                return nullptr;
            }

            FreeObject* obj = current_span->free_objects;
            current_span->free_objects = obj->next;
            --current_span->num_free_objects;
            --num_free;
            return obj;
        }

        void deallocate(void* ptr) {
            if (!ptr) return;

            Span* span = page_heap->span_for(ptr);
            if (!span) return;

            auto* obj = static_cast<FreeObject*>(ptr);
            obj->next = span->free_objects;
            span->free_objects = obj;
            ++span->num_free_objects;
            ++num_free;

            if (span->num_free_objects == span->total_objects) {
                release_to_page_heap(span);
            }
        }

        uint32_t get_num_free() const { return num_free; }

    private:
        SizeClassInfo sc_info;
        uint32_t num_free;
        Span* span_list;
        Span* current_span; // cached pointer to a span w/ free objects, making common case allocate in O(1)
        PageHeap* page_heap;

        Span* find_non_empty_span() {
            Span* s = span_list;
            while (s) {
                if (s->free_objects) return s;
                s = s->next;
            }
            return nullptr;
        }

        void fetch_from_page_heap() {
            if (sc_info.size == 0 || sc_info.pages == 0) return;

            // TODO: check if this is correct
            Span* span = page_heap->allocate_span(sc_info.pages, sc_info.size);
            if (!span) return;

            // Link span into the CFL's span tracking list (reuses
            // next/prev which are unused while the span is allocated).
            span->next = span_list;
            span->prev = nullptr;
            if (span_list) span_list->prev = span;
            span_list = span;

            auto* base = reinterpret_cast<std::byte*>(span->start * PAGE_SIZE);
            size_t total_bytes = static_cast<size_t>(span->num_pages) * PAGE_SIZE;
            size_t count = total_bytes / sc_info.size;

            span->total_objects = static_cast<uint32_t>(count);
            span->free_objects = nullptr;
            // span->num_free_objects = 0;

            // dividing Spans into objects of the corresponding size
            for (size_t i = 0; i < count; ++i) {
                auto* obj = reinterpret_cast<FreeObject*>(base + i * sc_info.size);
                obj->next = span->free_objects;
                span->free_objects = obj;
            }
            span->num_free_objects = static_cast<uint32_t>(count);
            num_free += static_cast<uint32_t>(count);

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

            span->free_objects = nullptr;
            span->num_free_objects = 0;
            span->total_objects = 0;
            page_heap->return_span(span);
        }
};

#endif
