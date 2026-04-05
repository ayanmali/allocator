/*
Restartable-sequence operations for per-CPU slab push/pop.

Each function atomically reads the CPU ID from the kernel-maintained
struct rseq in TLS, indexes into the per-CPU slab array, and performs
the stack operation.  If the thread is preempted or migrated during
the critical section, the kernel restarts execution from the top.

x86-64 Linux only (glibc >= 2.35 auto-registers rseq per thread).
Non-rseq fallback uses sched_getcpu() + plain C++ slab methods.
*/
#ifndef SLABS
#define SLABS

#include "config.hpp"
#include "size_classes.hpp"
#include <cstdint>
#include <cstddef>

/*
One slab per each CPU
Each slab stores 32 bit headers contiguously (one per size class) that contain indexes into the size class array of pointers
After the last header comes the first pointer array for the first size class.
Each pointer array's capacity should be a multiple of the corresponding size class's batch size

*/
struct SlabHeader {
    uint16_t current; // points to next valid slot
    uint16_t end; // one past the last valid slot
};

/*
Each slab represents a cache corresponding to one logical CPU.
Each slab contains an array of pointers, which is packed to contain pointers to objects across all size classes 
*/
struct Slab {
    SlabHeader headers[NUM_SIZE_CLASSES];
    void* pointers[]; // array of pointers across every size class

    Slab() {
        // set the current and end indices for each header
        for (uint32_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
            headers[i].current = SIZE_CLASS_OFFSETS[i];
            headers[i].end = SIZE_CLASS_OFFSETS[i + 1];
        }
    }

    void** push_destination(uint32_t size_class_idx) {
        return &pointers[headers[size_class_idx].current];
    }

    void commit_push(uint32_t size_class_idx, uint32_t n) {
        headers[size_class_idx].current += n;
    }

    void** pop_source(uint32_t size_class_idx, uint32_t n) {
        return &pointers[headers[size_class_idx].current - n];
    }

    void commit_pop(uint32_t size_class_idx, uint32_t n) {
        headers[size_class_idx].current -= n;
    }

    uint32_t available(uint32_t size_class_idx) {
        return headers[size_class_idx].current - SIZE_CLASS_OFFSETS[size_class_idx];
    }

    uint32_t remaining(uint32_t size_class_idx) {
        return headers[size_class_idx].end - headers[size_class_idx].current;
    }

    static uint32_t get_begin(uint32_t size_class_idx) {
        return SIZE_CLASS_OFFSETS[size_class_idx];
    }

};

/*
Popping from the stack
TODO: implement prefetching?
*/
inline void* fallback_allocate(Slab* slab, uint32_t size_class_idx) {
    auto& hdr = slab->headers[size_class_idx];
    if (hdr.current > SIZE_CLASS_OFFSETS[size_class_idx]) {
        auto idx = hdr.current - 1;
        auto ptr = slab->pointers[idx];
        hdr.current = idx; // rseq commit operation
        return ptr;
    }
    return nullptr;
}

/*
Pushing onto the stack
*/
inline bool fallback_deallocate(Slab* slab, void* mem, uint32_t size_class_idx) {
    auto& hdr = slab->headers[size_class_idx];
    if (hdr.current < hdr.end) {
        auto idx = hdr.current + 1;
        // attempt to push onto stack; if full, return false
        slab->pointers[idx] = mem;
        ++hdr.current;
        return true;
    }
    return false;
}

#if defined(__linux__)
#include <sched.h>
#endif

extern "C" {
    extern ptrdiff_t __rseq_offset;
    extern unsigned int __rseq_size;
}

static constexpr uint32_t RSEQ_SIG            = 0x53053053;
static constexpr int      RSEQ_CPU_ID_OFFSET  = 4;
static constexpr int      RSEQ_CS_OFFSET      = 8;
static constexpr size_t   SLAB_POINTERS_OFFSET =
    NUM_SIZE_CLASSES * sizeof(SlabHeader);

static inline char* get_rseq_ptr() {
    return reinterpret_cast<char*>(__builtin_thread_pointer()) + __rseq_offset;
}

// ---------------------------------------------------------------------------
// x86-64 rseq critical sections
// ---------------------------------------------------------------------------
#if defined(__linux__) && defined(__x86_64__)

/*
Pop one pointer from slab->pointers[] for size class `sc_idx`.
Returns the pointer, or nullptr if that size class's stack is empty.

`begin` is SIZE_CLASS_OFFSETS[sc_idx] (the empty-stack sentinel).
*/
static inline void* rseq_slab_pop(Slab** slabs,
                                   uint32_t sc_idx,
                                   uint32_t begin)
{
    void*    result;
    char*    rseq = get_rseq_ptr();
    uint64_t hdr_off = static_cast<uint64_t>(sc_idx) * sizeof(SlabHeader);

    __asm__ __volatile__ (
        /* ---- rseq_cs descriptor (in __rseq_cs section) ---- */
        ".pushsection __rseq_cs, \"aw\"\n\t"
        ".balign 32\n\t"
        "11:\n\t"
        ".long 0, 0\n\t"
        ".quad 12f, 13f, 14f\n\t"
        ".popsection\n\t"
        ".pushsection __rseq_cs_ptr_array, \"aw\"\n\t"
        ".quad 11b\n\t"
        ".popsection\n\t"

        /* ---- critical section start ---- */
        "12:\n\t"
        "leaq 11b(%%rip), %%rax\n\t"
        "movq %%rax, %c[cs_off](%[rseq])\n\t"

        "movl %c[cpu_off](%[rseq]), %%ecx\n\t"
        "movq (%[slabs], %%rcx, 8), %%r8\n\t"

        "movzwl (%%r8, %[hdr_off], 1), %%eax\n\t"
        "cmpl %k[begin], %%eax\n\t"
        "jbe 15f\n\t"

        "decl %%eax\n\t"
        "movq %c[ptr_off](%%r8, %%rax, 8), %%r9\n\t"

        /* COMMIT: write decremented current back to header */
        "movw %%ax, (%%r8, %[hdr_off], 1)\n\t"

        /* ---- post-commit ---- */
        "13:\n\t"
        "movq %%r9, %[result]\n\t"
        "jmp 16f\n\t"

        /* ---- empty ---- */
        "15:\n\t"
        "xorq %[result], %[result]\n\t"
        "jmp 16f\n\t"

        /* ---- abort handler (signature must precede label) ---- */
        ".long %c[sig]\n\t"
        "14:\n\t"
        "jmp 12b\n\t"

        /* ---- done ---- */
        "16:\n\t"

        : [result] "=&r" (result)
        : [slabs]   "r" (slabs),
          [hdr_off]  "r" (hdr_off),
          [begin]    "r" (static_cast<uint64_t>(begin)),
          [rseq]     "r" (rseq),
          [cs_off]   "i" (RSEQ_CS_OFFSET),
          [cpu_off]  "i" (RSEQ_CPU_ID_OFFSET),
          [ptr_off]  "i" (SLAB_POINTERS_OFFSET),
          [sig]      "i" (RSEQ_SIG)
        : "rax", "rcx", "r8", "r9", "memory", "cc"
    );
    return result;
}

/*
Push `ptr` onto slab->pointers[] for size class `sc_idx`.
Returns true on success, false if that size class's stack is full.
*/
static inline bool rseq_slab_push(Slab** slabs,
                                   uint32_t sc_idx,
                                   void* ptr)
{
    int      ok;
    char*    rseq = get_rseq_ptr();
    uint64_t hdr_off = static_cast<uint64_t>(sc_idx) * sizeof(SlabHeader);

    __asm__ __volatile__ (
        /* ---- rseq_cs descriptor ---- */
        ".pushsection __rseq_cs, \"aw\"\n\t"
        ".balign 32\n\t"
        "21:\n\t"
        ".long 0, 0\n\t"
        ".quad 22f, 23f, 24f\n\t"
        ".popsection\n\t"
        ".pushsection __rseq_cs_ptr_array, \"aw\"\n\t"
        ".quad 21b\n\t"
        ".popsection\n\t"

        /* ---- critical section start ---- */
        "22:\n\t"
        "leaq 21b(%%rip), %%rax\n\t"
        "movq %%rax, %c[cs_off](%[rseq])\n\t"

        "movl %c[cpu_off](%[rseq]), %%ecx\n\t"
        "movq (%[slabs], %%rcx, 8), %%r8\n\t"

        "movzwl (%%r8, %[hdr_off], 1), %%eax\n\t"
        "movzwl 2(%%r8, %[hdr_off], 1), %%r9d\n\t"
        "cmpl %%r9d, %%eax\n\t"
        "jge 25f\n\t"

        /* Store pointer into the free slot (beyond current stack top) */
        "movq %[ptr], %c[ptr_off](%%r8, %%rax, 8)\n\t"

        /* COMMIT: write current + 1 back to header */
        "incl %%eax\n\t"
        "movw %%ax, (%%r8, %[hdr_off], 1)\n\t"

        /* ---- post-commit ---- */
        "23:\n\t"
        "movl $1, %k[ok]\n\t"
        "jmp 26f\n\t"

        /* ---- full ---- */
        "25:\n\t"
        "xorl %k[ok], %k[ok]\n\t"
        "jmp 26f\n\t"

        /* ---- abort handler ---- */
        ".long %c[sig]\n\t"
        "24:\n\t"
        "jmp 22b\n\t"

        /* ---- done ---- */
        "26:\n\t"

        : [ok] "=&r" (ok)
        : [slabs]   "r" (slabs),
          [hdr_off]  "r" (hdr_off),
          [ptr]      "r" (ptr),
          [rseq]     "r" (rseq),
          [cs_off]   "i" (RSEQ_CS_OFFSET),
          [cpu_off]  "i" (RSEQ_CPU_ID_OFFSET),
          [ptr_off]  "i" (SLAB_POINTERS_OFFSET),
          [sig]      "i" (RSEQ_SIG)
        : "rax", "rcx", "r8", "r9", "memory", "cc"
    );
    return ok != 0;
}

#endif /* __linux__ && __x86_64__ */

// ---------------------------------------------------------------------------
// Unified wrappers (rseq when available, plain fallback otherwise)
// ---------------------------------------------------------------------------

static inline int fallback_get_cpu() {
#if defined(__linux__)
    return sched_getcpu();
#else
    return 0;
#endif
}

// TODO: implement prefetching?
static inline void* slab_pop(Slab** slabs,
                              uint32_t sc_idx,
                              uint32_t begin)
{
#if defined(__linux__) && defined(__x86_64__)
    if (__builtin_expect(__rseq_size > 0, 1))
        return rseq_slab_pop(slabs, sc_idx, begin);
#endif
    return fallback_allocate(slabs[fallback_get_cpu()], sc_idx);
}

static inline bool slab_push(Slab** slabs,
                              uint32_t sc_idx,
                              void* ptr)
{
#if defined(__linux__) && defined(__x86_64__)
    if (__builtin_expect(__rseq_size > 0, 1))
        return rseq_slab_push(slabs, sc_idx, ptr);
#endif
    return fallback_deallocate(slabs[fallback_get_cpu()], ptr, sc_idx);
}

#endif /* RSEQ_OPS */
