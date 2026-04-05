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
Each slab stores one 16-bit count per size class, followed by pointer
storage for every size class. Pointer ranges are precomputed so each
size class still owns a fixed slice of the pointer array.

*/
static constexpr size_t align_up(size_t value, size_t alignment) {
    return alignment == 0
        ? value
        : ((value + alignment - 1) / alignment) * alignment;
}

/*
Implemented as one contiguous mmap'd region.
Each slab represents a cache corresponding to one logical CPU.
Each slab contains a compact array of per-size-class counts followed by
cache-line-aligned pointer storage. Zero-filled anonymous pages already
represent a valid empty slab, so untouched slabs stay lazily faulted in.
*/
struct Slabs {
    uint16_t currents[NUM_SIZE_CLASSES];
    void* pointers[]; // array of pointers across every size class

    void** push_destination(uint32_t sc_idx) {
        return &pointers[SIZE_CLASS_OFFSETS[sc_idx] + currents[sc_idx]];
    }

    void commit_push(uint32_t sc_idx, uint32_t n) {
        currents[sc_idx] = static_cast<uint16_t>(currents[sc_idx] + n);
    }

    void** pop_source(uint32_t sc_idx, uint32_t n) {
        return &pointers[SIZE_CLASS_OFFSETS[sc_idx] + currents[sc_idx] - n];
    }

    void commit_pop(uint32_t sc_idx, uint32_t n) {
        currents[sc_idx] = static_cast<uint16_t>(currents[sc_idx] - n);
    }

    uint32_t available(uint32_t sc_idx) {
        return currents[sc_idx];
    }

    uint32_t remaining(uint32_t sc_idx) {
        return SIZE_CLASS_CAPACITIES[sc_idx] - currents[sc_idx];
    }
};

/*
Popping from the stack
TODO: implement prefetching?
*/
inline void* fallback_allocate(Slabs* slab, uint32_t sc_idx) {
    uint16_t cur = slab->currents[sc_idx];
    if (cur > 0) {
        uint32_t idx = SIZE_CLASS_OFFSETS[sc_idx] + cur - 1;
        void* ptr = slab->pointers[idx];
        slab->currents[sc_idx] = static_cast<uint16_t>(cur - 1);
        return ptr;
    }
    return nullptr;
}

/*
Pushing onto the stack
*/
inline bool fallback_deallocate(Slabs* slab, void* mem, uint32_t sc_idx) {
    uint16_t cur = slab->currents[sc_idx];
    if (cur < SIZE_CLASS_CAPACITIES[sc_idx]) {
        uint32_t idx = SIZE_CLASS_OFFSETS[sc_idx] + cur;
        slab->pointers[idx] = mem;
        slab->currents[sc_idx] = static_cast<uint16_t>(cur + 1);
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
static constexpr size_t   SLAB_HEADERS_BYTES =
    align_up(NUM_SIZE_CLASSES * sizeof(uint16_t), CACHE_LINE_SIZE);
static constexpr size_t   SLAB_POINTERS_OFFSET = SLAB_HEADERS_BYTES;
static constexpr size_t   SLAB_RAW_BYTE_SIZE =
    SLAB_POINTERS_OFFSET + SIZE_CLASS_OFFSETS[NUM_SIZE_CLASSES] * sizeof(void*);
static constexpr size_t   SLAB_STRIDE_ALIGNMENT =
    PAGE_ALIGN_SLAB_STRIDE ? PAGE_SIZE : CACHE_LINE_SIZE;
static constexpr size_t   SLAB_STRIDE_BYTES =
    align_up(SLAB_RAW_BYTE_SIZE, SLAB_STRIDE_ALIGNMENT);

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

*/
static inline void* rseq_slab_pop(Slabs* slabs_base,
                                   uint32_t sc_idx)
{
    void*    result;
    char*    rseq = get_rseq_ptr();
    char*    base = reinterpret_cast<char*>(slabs_base);
    const uint32_t* offsets = SIZE_CLASS_OFFSETS.data();
    uint64_t cur_off = static_cast<uint64_t>(sc_idx) * sizeof(uint16_t);

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
        "imulq %[stride], %%rcx, %%r8\n\t"
        "addq %[base], %%r8\n\t"

        "movzwl (%%r8, %[cur_off], 1), %%eax\n\t"
        "testl %%eax, %%eax\n\t"
        "jbe 15f\n\t"

        "leal -1(%%eax), %%edx\n\t"
        "movl (%[offsets], %[sc_idx], 4), %%eax\n\t"
        "addl %%edx, %%eax\n\t"
        "movq %c[ptr_off](%%r8, %%rax, 8), %%r9\n\t"

        /* COMMIT: write decremented current back */
        "movw %%dx, (%%r8, %[cur_off], 1)\n\t"

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
        : [base]     "r" (base),
          [cur_off]  "r" (cur_off),
          [offsets]  "r" (offsets),
          [sc_idx]   "r" (static_cast<uint64_t>(sc_idx)),
          [rseq]     "r" (rseq),
          [stride]   "i" (SLAB_STRIDE_BYTES),
          [cs_off]   "i" (RSEQ_CS_OFFSET),
          [cpu_off]  "i" (RSEQ_CPU_ID_OFFSET),
          [ptr_off]  "i" (SLAB_POINTERS_OFFSET),
          [sig]      "i" (RSEQ_SIG)
        : "rax", "rcx", "rdx", "r8", "r9", "memory", "cc"
    );
    return result;
}

/*
Push `ptr` onto slab->pointers[] for size class `sc_idx`.
Returns true on success, false if that size class's stack is full.
*/
static inline bool rseq_slab_push(Slabs* slabs_base,
                                   uint32_t sc_idx,
                                   void* ptr)
{
    int      ok;
    char*    rseq = get_rseq_ptr();
    char*    base = reinterpret_cast<char*>(slabs_base);
    const uint32_t* offsets = SIZE_CLASS_OFFSETS.data();
    const uint32_t* capacities = SIZE_CLASS_CAPACITIES.data();
    uint64_t cur_off = static_cast<uint64_t>(sc_idx) * sizeof(uint16_t);

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
        "imulq %[stride], %%rcx, %%r8\n\t"
        "addq %[base], %%r8\n\t"

        "movzwl (%%r8, %[cur_off], 1), %%eax\n\t"
        "movl (%[caps], %[sc_idx], 4), %%r9d\n\t"
        "cmpl %%r9d, %%eax\n\t"
        "jge 25f\n\t"

        "movl (%[offsets], %[sc_idx], 4), %%r10d\n\t"
        "addl %%eax, %%r10d\n\t"
        /* Store pointer into the free slot (beyond current stack top) */
        "movq %[ptr], %c[ptr_off](%%r8, %%r10, 8)\n\t"

        /* COMMIT: write current + 1 back */
        "incl %%eax\n\t"
        "movw %%ax, (%%r8, %[cur_off], 1)\n\t"

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
        : [base]     "r" (base),
          [cur_off]  "r" (cur_off),
          [offsets]  "r" (offsets),
          [caps]     "r" (capacities),
          [sc_idx]   "r" (static_cast<uint64_t>(sc_idx)),
          [ptr]      "r" (ptr),
          [rseq]     "r" (rseq),
          [stride]   "i" (SLAB_STRIDE_BYTES),
          [cs_off]   "i" (RSEQ_CS_OFFSET),
          [cpu_off]  "i" (RSEQ_CPU_ID_OFFSET),
          [ptr_off]  "i" (SLAB_POINTERS_OFFSET),
          [sig]      "i" (RSEQ_SIG)
        : "rax", "rcx", "r8", "r9", "r10", "memory", "cc"
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

static inline Slabs* get_slabs(Slabs* slabs_base, uint32_t cpu_id) {
    return reinterpret_cast<Slabs*>(
        reinterpret_cast<char*>(slabs_base) + cpu_id * SLAB_STRIDE_BYTES);
}

// TODO: implement prefetching?
static inline void* slab_pop(Slabs* slabs_base,
                              uint32_t sc_idx)
{
#if defined(__linux__) && defined(__x86_64__)
    if (__builtin_expect(__rseq_size > 0, 1))
        return rseq_slab_pop(slabs_base, sc_idx);
#endif
    return fallback_allocate(get_slabs(slabs_base, fallback_get_cpu()), sc_idx);
}

static inline bool slab_push(Slabs* slabs_base,
                              uint32_t sc_idx,
                              void* ptr)
{
#if defined(__linux__) && defined(__x86_64__)
    if (__builtin_expect(__rseq_size > 0, 1))
        return rseq_slab_push(slabs_base, sc_idx, ptr);
#endif
    return fallback_deallocate(get_slabs(slabs_base, fallback_get_cpu()), ptr, sc_idx);
}

#endif /* RSEQ_OPS */
