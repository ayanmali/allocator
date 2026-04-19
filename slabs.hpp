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
#if defined(__linux__)
#include <sched.h>
#include <sys/rseq.h>
#endif

/*
One slab per each CPU
Each slab stores one 16-bit count per size class, followed by pointer
storage for every size class. Pointer ranges are precomputed so each
size class still owns a fixed slice of the pointer array.

*/

// extern "C" {
//     extern struct rseq __rseq_abi __attribute__((tls_model("initial-exec")));
// }

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
    alignas(CACHE_LINE_SIZE) void* pointers[]; // array of pointers across every size class

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

static constexpr uint32_t RSEQ_ABORT_SIGNATURE = 0x53053053;
#if defined(__linux__) && defined(__x86_64__)
static constexpr int      RSEQ_CPU_ID_START_OFFSET =
    static_cast<int>(offsetof(struct rseq, cpu_id_start));
static constexpr int      RSEQ_CPU_ID_OFFSET =
    static_cast<int>(offsetof(struct rseq, cpu_id));
static constexpr int      RSEQ_CS_OFFSET =
    static_cast<int>(offsetof(struct rseq, rseq_cs));
#else
static constexpr int      RSEQ_CPU_ID_START_OFFSET = 0;
static constexpr int      RSEQ_CPU_ID_OFFSET = 0;
static constexpr int      RSEQ_CS_OFFSET = 0;
#endif
static constexpr size_t   SLAB_POINTERS_OFFSET =
    align_up(NUM_SIZE_CLASSES * sizeof(uint16_t), CACHE_LINE_SIZE);

static constexpr size_t   SLAB_RAW_BYTE_SIZE =
    SLAB_POINTERS_OFFSET + SIZE_CLASS_OFFSETS[NUM_SIZE_CLASSES] * sizeof(void*);

static constexpr size_t   SLAB_STRIDE_ALIGNMENT =
    PAGE_ALIGN_SLAB_STRIDE ? PAGE_SIZE : CACHE_LINE_SIZE;

static constexpr size_t   SLAB_STRIDE_BYTES =
    align_up(SLAB_RAW_BYTE_SIZE, SLAB_STRIDE_ALIGNMENT);

static_assert(offsetof(Slabs, pointers) == SLAB_POINTERS_OFFSET,
              "Slabs pointer storage layout must match the rseq/fallback offsets");

static inline char* get_rseq_ptr() {
    return reinterpret_cast<char*>(__builtin_thread_pointer()) + __rseq_offset;
}

static inline bool rseq_available() {
#if defined(__linux__) && defined(__x86_64__)
    return __rseq_size >= sizeof(struct rseq);
#else
    return false;
#endif
}

static inline uint32_t fallback_get_cpu() {
    auto cpu_id = sched_getcpu();
    if (cpu_id >= 0) {
        return static_cast<uint32_t>(cpu_id);
    }
    unsigned int cpu, numa;
    if (getcpu(&cpu, &numa) >= 0) {
        return static_cast<uint32_t>(cpu);
    }
    return 0;
};

static inline uint32_t current_cpu_id() {
#if defined(__linux__) && defined(__x86_64__)
    if (rseq_available()) {
        auto* rseq = reinterpret_cast<volatile struct rseq*>(get_rseq_ptr());
        int32_t cpu_id = static_cast<int32_t>(rseq->cpu_id);
        if (cpu_id >= 0) {
            return static_cast<uint32_t>(cpu_id);
        }
    }
#endif
    return fallback_get_cpu();
}

struct RseqSlabPopResult {
    void* ptr;
    bool used_fastpath;
};

struct RseqSlabPushResult {
    bool ok;
    bool used_fastpath;
};

// ---------------------------------------------------------------------------
// x86-64 rseq critical sections
// ---------------------------------------------------------------------------
#if defined(__linux__) && defined(__x86_64__)

/*
Pop one pointer from slab->pointers[] for size class `sc_idx`.
Returns the pointer, or nullptr if that size class's stack is empty.

*/
static inline RseqSlabPopResult rseq_slab_pop(Slabs* slabs_base,
                                              uint32_t slab_count,
                                              uint32_t sc_idx)
{
    void*    result;
    int      used_fastpath;
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
        ".quad 12f, 13f - 12f, 14f\n\t"
        ".popsection\n\t"
        ".pushsection __rseq_cs_ptr_array, \"aw\"\n\t"
        ".quad 11b\n\t"
        ".popsection\n\t"

        /* ---- critical section start ---- */
        "12:\n\t"
        "leaq 11b(%%rip), %%rax\n\t"
        "movq %%rax, %c[cs_off](%[rseq])\n\t"

        "movl %c[cpu_start_off](%[rseq]), %%r10d\n\t"
        "cmpl %k[slab_count], %%r10d\n\t"
        "jae 17f\n\t"
        "imulq %[stride], %%r10, %%r8\n\t"
        "addq %[base], %%r8\n\t"

        "movzwl (%%r8, %[cur_off], 1), %%eax\n\t"
        "movl %c[cpu_off](%[rseq]), %%r11d\n\t"
        "cmpl %%r10d, %%r11d\n\t"
        "jne 12b\n\t"
        "testl %%eax, %%eax\n\t"
        "jbe 15f\n\t"

        "leal -1(%%eax), %%edx\n\t"
        "movl (%[offsets], %[sc_idx], 4), %%eax\n\t"
        "addl %%edx, %%eax\n\t"
        "movq %c[ptr_off](%%r8, %%rax, 8), %%r9\n\t"

        /* COMMIT: write decremented current back */
        "movw %%dx, (%%r8, %[cur_off], 1)\n\t"
        "xorq %%rax, %%rax\n\t"
        "movq %%rax, %c[cs_off](%[rseq])\n\t"
        "movl $1, %k[used_fastpath]\n\t"

        /* ---- post-commit ---- */
        "13:\n\t"
        "movq %%r9, %[result]\n\t"
        "jmp 16f\n\t"

        /* ---- empty ---- */
        "15:\n\t"
        "xorq %%rax, %%rax\n\t"
        "movq %%rax, %c[cs_off](%[rseq])\n\t"
        "movl $1, %k[used_fastpath]\n\t"
        "xorq %[result], %[result]\n\t"
        "jmp 16f\n\t"

        /* ---- CPU index out of range ---- */
        "17:\n\t"
        "xorq %%rax, %%rax\n\t"
        "movq %%rax, %c[cs_off](%[rseq])\n\t"
        "xorl %k[used_fastpath], %k[used_fastpath]\n\t"
        "xorq %[result], %[result]\n\t"
        "jmp 16f\n\t"

        /* ---- abort handler ---- */
        ".byte 0x0f, 0xb9, 0x3d\n\t"
        ".long %c[sig]\n\t"
        "14:\n\t"
        "jmp 12b\n\t"

        /* ---- done ---- */
        "16:\n\t"

        : [result] "=&r" (result),
          [used_fastpath] "=r" (used_fastpath)
        : [base]     "r" (base),
          [cur_off]  "r" (cur_off),
          [offsets]  "r" (offsets),
          [slab_count] "r" (slab_count),
          [sc_idx]   "r" (static_cast<uint64_t>(sc_idx)),
          [rseq]     "r" (rseq),
          [stride]   "i" (SLAB_STRIDE_BYTES),
          [cpu_start_off] "i" (RSEQ_CPU_ID_START_OFFSET),
          [cs_off]   "i" (RSEQ_CS_OFFSET),
          [cpu_off]  "i" (RSEQ_CPU_ID_OFFSET),
          [ptr_off]  "i" (SLAB_POINTERS_OFFSET),
          [sig]      "i" (RSEQ_ABORT_SIGNATURE)
        : "rax", "rdx", "r8", "r9", "r10", "r11", "memory", "cc"
    );
    return RseqSlabPopResult{result, used_fastpath != 0};
}

/*
Push `ptr` onto slab->pointers[] for size class `sc_idx`.
Returns true on success, false if that size class's stack is full.
*/
static inline RseqSlabPushResult rseq_slab_push(Slabs* slabs_base,
                                                uint32_t slab_count,
                                                uint32_t sc_idx,
                                                void* ptr)
{
    int      ok;
    int      used_fastpath;
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
        ".quad 22f, 23f - 22f, 24f\n\t"
        ".popsection\n\t"
        ".pushsection __rseq_cs_ptr_array, \"aw\"\n\t"
        ".quad 21b\n\t"
        ".popsection\n\t"

        /* ---- critical section start ---- */
        "22:\n\t"
        "leaq 21b(%%rip), %%rax\n\t"
        "movq %%rax, %c[cs_off](%[rseq])\n\t"

        "movl %c[cpu_start_off](%[rseq]), %%r10d\n\t"
        "cmpl %k[slab_count], %%r10d\n\t"
        "jae 27f\n\t"
        "imulq %[stride], %%r10, %%r8\n\t"
        "addq %[base], %%r8\n\t"

        "movzwl (%%r8, %[cur_off], 1), %%eax\n\t"
        "movl (%[caps], %[sc_idx], 4), %%r9d\n\t"
        "movl %c[cpu_off](%[rseq]), %%r11d\n\t"
        "cmpl %%r10d, %%r11d\n\t"
        "jne 22b\n\t"
        "cmpl %%r9d, %%eax\n\t"
        "jge 25f\n\t"

        "movl (%[offsets], %[sc_idx], 4), %%r10d\n\t"
        "addl %%eax, %%r10d\n\t"
        /* Store pointer into the free slot (beyond current stack top) */
        "movq %[ptr], %c[ptr_off](%%r8, %%r10, 8)\n\t"

        /* COMMIT: write current + 1 back */
        "incl %%eax\n\t"
        "movw %%ax, (%%r8, %[cur_off], 1)\n\t"
        "xorq %%rax, %%rax\n\t"
        "movq %%rax, %c[cs_off](%[rseq])\n\t"
        "movl $1, %k[used_fastpath]\n\t"

        /* ---- post-commit ---- */
        "23:\n\t"
        "movl $1, %k[ok]\n\t"
        "jmp 26f\n\t"

        /* ---- full ---- */
        "25:\n\t"
        "xorq %%rax, %%rax\n\t"
        "movq %%rax, %c[cs_off](%[rseq])\n\t"
        "movl $1, %k[used_fastpath]\n\t"
        "xorl %k[ok], %k[ok]\n\t"
        "jmp 26f\n\t"

        /* ---- CPU index out of range ---- */
        "27:\n\t"
        "xorq %%rax, %%rax\n\t"
        "movq %%rax, %c[cs_off](%[rseq])\n\t"
        "xorl %k[used_fastpath], %k[used_fastpath]\n\t"
        "xorl %k[ok], %k[ok]\n\t"
        "jmp 26f\n\t"

        /* ---- abort handler ---- */
        ".byte 0x0f, 0xb9, 0x3d\n\t"
        ".long %c[sig]\n\t"
        "24:\n\t"
        "jmp 22b\n\t"

        /* ---- done ---- */
        "26:\n\t"

        : [ok] "=r" (ok),
          [used_fastpath] "=r" (used_fastpath)
        : [base]     "r" (base),
          [cur_off]  "r" (cur_off),
          [offsets]  "r" (offsets),
          [caps]     "r" (capacities),
          [slab_count] "r" (slab_count),
          [sc_idx]   "r" (static_cast<uint64_t>(sc_idx)),
          [ptr]      "r" (ptr),
          [rseq]     "r" (rseq),
          [stride]   "i" (SLAB_STRIDE_BYTES),
          [cpu_start_off] "i" (RSEQ_CPU_ID_START_OFFSET),
          [cs_off]   "i" (RSEQ_CS_OFFSET),
          [cpu_off]  "i" (RSEQ_CPU_ID_OFFSET),
          [ptr_off]  "i" (SLAB_POINTERS_OFFSET),
          [sig]      "i" (RSEQ_ABORT_SIGNATURE)
        : "rax", "r8", "r9", "r10", "r11", "memory", "cc"
    );
    return RseqSlabPushResult{ok != 0, used_fastpath != 0};
}

#endif /* __linux__ && __x86_64__ */

// ---------------------------------------------------------------------------
// Unified wrappers (rseq when available, plain fallback otherwise)
// ---------------------------------------------------------------------------

static inline Slabs* get_slabs(Slabs* slabs_base, uint32_t cpu_id) {
    return reinterpret_cast<Slabs*>(
        reinterpret_cast<char*>(slabs_base) + cpu_id * SLAB_STRIDE_BYTES);
}

static inline Slabs* get_slabs_checked(Slabs* slabs_base,
                                       uint32_t slab_count,
                                       uint32_t cpu_id)
{
    if (cpu_id >= slab_count) return nullptr;
    return get_slabs(slabs_base, cpu_id);
}

// TODO: implement prefetching?
static inline void* slab_pop(Slabs* slabs_base,
                              uint32_t slab_count,
                              uint32_t sc_idx)
{
#if defined(__linux__) && defined(__x86_64__)
    if (__builtin_expect(rseq_available(), 1)) {
        RseqSlabPopResult result = rseq_slab_pop(slabs_base, slab_count, sc_idx);
        if (result.used_fastpath) {
            return result.ptr;
        }
    }
#endif
    Slabs* slab = get_slabs_checked(slabs_base, slab_count, current_cpu_id());
    return slab ? fallback_allocate(slab, sc_idx) : nullptr;
}

static inline bool slab_push(Slabs* slabs_base,
                              uint32_t slab_count,
                              uint32_t sc_idx,
                              void* ptr)
{
#if defined(__linux__) && defined(__x86_64__)
    if (__builtin_expect(rseq_available(), 1)) {
        RseqSlabPushResult result = rseq_slab_push(slabs_base, slab_count, sc_idx, ptr);
        if (result.used_fastpath) {
            return result.ok;
        }
    }
#endif
    Slabs* slab = get_slabs_checked(slabs_base, slab_count, current_cpu_id());
    return slab ? fallback_deallocate(slab, ptr, sc_idx) : false;
}

#endif /* RSEQ_OPS */
