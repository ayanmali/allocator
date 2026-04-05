#include "old_design.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <unordered_set>
#include <vector>

namespace {

bool test_return_back_reuses_freelist() {
    std::cout << "\n[TEST] return_back adds block to free list and reuses it\n";
    Allocator allocator{};

    auto* first = static_cast<std::uint8_t*>(allocator.allocate(128));
    if (!first || first == reinterpret_cast<void*>(-1)) {
        std::cout << "[FAIL] initial allocation failed\n";
        return false;
    }
    std::memset(first, 0xAB, 128);

    if (!allocator.return_back(first)) {
        std::cout << "[FAIL] return_back failed\n";
        return false;
    }

    auto* reused = static_cast<std::uint8_t*>(allocator.allocate(64));
    if (reused != first) {
        std::cout << "[FAIL] expected reuse of returned block pointer; expected="
                  << static_cast<void*>(first) << " got=" << static_cast<void*>(reused) << "\n";
        return false;
    }

    std::cout << "[PASS] returned block was reused from free list\n";
    return true;
}

bool test_no_new_block_when_free_space_exists() {
    std::cout << "\n[TEST] allocator does not create new block when free-list space is enough\n";
    Allocator allocator{};

    auto* p = static_cast<std::uint8_t*>(allocator.allocate(256));
    if (!p || p == reinterpret_cast<void*>(-1)) {
        std::cout << "[FAIL] initial allocation failed\n";
        return false;
    }

    if (!allocator.return_back(p)) {
        std::cout << "[FAIL] return_back failed\n";
        return false;
    }

    void* brk_before = sbrk(0);
    auto* q = static_cast<std::uint8_t*>(allocator.allocate(128));
    void* brk_after = sbrk(0);
    if (!q || q == reinterpret_cast<void*>(-1)) {
        std::cout << "[FAIL] second allocation failed\n";
        return false;
    }

    if (brk_before != brk_after) {
        std::cout << "[FAIL] program break moved despite enough free-list space\n";
        std::cout << "       before=" << brk_before << ", after=" << brk_after << "\n";
        return false;
    }

    std::cout << "[PASS] no additional brk growth for reusable free-list allocation\n";
    return true;
}

bool test_coalescing_of_adjacent_freed_blocks() {
    std::cout << "\n[TEST] adjacent freed blocks are coalesced and reusable as a larger block\n";
    Allocator allocator{};

    auto* a = static_cast<std::uint8_t*>(allocator.allocate(120));
    auto* b = static_cast<std::uint8_t*>(allocator.allocate(120));
    auto* c = static_cast<std::uint8_t*>(allocator.allocate(120));
    if (!a || !b || !c || a == reinterpret_cast<void*>(-1) ||
        b == reinterpret_cast<void*>(-1) || c == reinterpret_cast<void*>(-1)) {
        std::cout << "[FAIL] setup allocations failed\n";
        return false;
    }

    if (!allocator.return_back(a) || !allocator.return_back(b) || !allocator.return_back(c)) {
        std::cout << "[FAIL] return_back on setup blocks failed\n";
        return false;
    }

    // If coalescing works, this should come from merged A+B+C region and reuse A's start.
    auto* merged = static_cast<std::uint8_t*>(allocator.allocate(280));
    if (merged != a) {
        std::cout << "[FAIL] expected coalesced allocation to start at first freed block\n";
        std::cout << "       expected=" << static_cast<void*>(a)
                  << " got=" << static_cast<void*>(merged) << "\n";
        return false;
    }

    std::cout << "[PASS] coalesced region served a larger allocation\n";
    return true;
}

bool test_pointer_arithmetic_and_boundary_safety() {
    std::cout << "\n[TEST] pointer arithmetic/boundary sanity through neighbor isolation\n";
    Allocator allocator{};

    auto* left = static_cast<std::uint8_t*>(allocator.allocate(80));
    auto* right = static_cast<std::uint8_t*>(allocator.allocate(80));
    if (!left || !right || left == reinterpret_cast<void*>(-1) || right == reinterpret_cast<void*>(-1)) {
        std::cout << "[FAIL] setup allocations failed\n";
        return false;
    }

    std::memset(left, 0x11, 80);
    std::memset(right, 0x22, 80);

    left[79] = 0x7E; // touch boundary byte in left block
    right[0] = 0x3C; // touch first byte in right block

    if (left[79] != 0x7E || right[0] != 0x3C) {
        std::cout << "[FAIL] boundary writes were not preserved\n";
        return false;
    }

    for (size_t i = 1; i < 80; ++i) {
        if (right[i] != 0x22) {
            std::cout << "[FAIL] right block corruption detected at offset " << i << "\n";
            return false;
        }
    }

    std::cout << "[PASS] adjacent blocks remain isolated under boundary writes\n";
    return true;
}

bool test_stress_random_allocate_return_uniqueness() {
    std::cout << "\n[TEST] stress random allocate/return with live-pointer uniqueness checks\n";
    Allocator allocator{};

    struct LiveAlloc {
        std::uint8_t* ptr;
        size_t size;
        std::uint8_t tag;
    };

    std::vector<LiveAlloc> live;
    std::unordered_set<void*> live_ptrs;
    std::mt19937 rng(0xC0FFEE);
    std::uniform_int_distribution<int> action_dist(0, 99);
    std::uniform_int_distribution<int> size_dist(1, 320);

    constexpr int iterations = 1000;
    for (int i = 0; i < iterations; ++i) {
        const bool do_allocate = live.empty() || action_dist(rng) < 65;
        if (do_allocate) {
            const size_t size = static_cast<size_t>(size_dist(rng));
            auto* ptr = static_cast<std::uint8_t*>(allocator.allocate(size));
            if (!ptr || ptr == reinterpret_cast<void*>(-1)) {
                std::cout << "[FAIL] allocation failed during stress loop at iter " << i << "\n";
                return false;
            }
            if (live_ptrs.count(ptr)) {
                std::cout << "[FAIL] duplicate live pointer allocated: " << static_cast<void*>(ptr) << "\n";
                return false;
            }

            const std::uint8_t tag = static_cast<std::uint8_t>((i * 37) & 0xFF);
            std::memset(ptr, tag, size);
            live.push_back({ptr, size, tag});
            live_ptrs.insert(ptr);
        } else {
            const size_t idx = static_cast<size_t>(rng() % live.size());
            LiveAlloc victim = live[idx];

            const size_t mid = victim.size / 2;
            if (victim.ptr[0] != victim.tag || victim.ptr[mid] != victim.tag ||
                victim.ptr[victim.size - 1] != victim.tag) {
                std::cout << "[FAIL] payload corruption before return_back at iter " << i << "\n";
                return false;
            }

            if (!allocator.return_back(victim.ptr)) {
                std::cout << "[FAIL] return_back failed during stress loop at iter " << i << "\n";
                return false;
            }
            live_ptrs.erase(victim.ptr);
            live[idx] = live.back();
            live.pop_back();
        }

        if (i % 250 == 0) {
            std::unordered_set<void*> verify_unique;
            for (const auto& alloc : live) {
                if (!verify_unique.insert(alloc.ptr).second) {
                    std::cout << "[FAIL] duplicate live pointer found during periodic check\n";
                    return false;
                }
            }
        }
    }

    for (const auto& alloc : live) {
        if (!allocator.return_back(alloc.ptr)) {
            std::cout << "[FAIL] cleanup return_back failed in stress test\n";
            return false;
        }
    }

    std::cout << "[PASS] stress test completed with no live-pointer duplicates\n";
    return true;
}

bool test_prev_search_end_start_and_wraparound() {
    std::cout << "\n[TEST] allocate starts from prev_search_end and wrap-around logic works\n";
    Allocator allocator{};

    auto* p1 = static_cast<std::uint8_t*>(allocator.allocate(64));
    auto* p2 = static_cast<std::uint8_t*>(allocator.allocate(64));
    auto* p3 = static_cast<std::uint8_t*>(allocator.allocate(64));
    if (!p1 || !p2 || !p3 ||
        p1 == reinterpret_cast<void*>(-1) ||
        p2 == reinterpret_cast<void*>(-1) ||
        p3 == reinterpret_cast<void*>(-1)) {
        std::cout << "[FAIL] setup allocations failed\n";
        return false;
    }

    // Insert a small free block at list head; large remainder stays after it.
    if (!allocator.return_back(p2)) {
        std::cout << "[FAIL] return_back failed for setup block\n";
        return false;
    }

    Block* head = allocator.debug_free_list_head();
    if (!head || !head->next) {
        std::cout << "[FAIL] expected at least two free blocks for traversal test\n";
        return false;
    }

    const size_t head_size = head->size;
    const size_t second_size = head->next->size;
    if (head_size + 1 > second_size) {
        std::cout << "[FAIL] unexpected free-list sizes for controlled traversal\n";
        return false;
    }

    // Force allocation to skip head and allocate from second node, updating prev_search_end=head.
    auto* from_second = static_cast<std::uint8_t*>(allocator.allocate(head_size + 1));
    if (!from_second || from_second == reinterpret_cast<void*>(-1)) {
        std::cout << "[FAIL] failed to allocate from second free-list node\n";
        return false;
    }

    auto trace1 = allocator.debug_last_allocate_trace();
    if (trace1.prev_search_end_after != head) {
        std::cout << "[FAIL] prev_search_end was not updated to previous node as expected\n";
        return false;
    }

    // Shrink head->next into a tiny remainder so a later request must wrap to head.
    head = allocator.debug_free_list_head();
    if (!head || !head->next) {
        std::cout << "[FAIL] expected second free node before wrap-around setup\n";
        return false;
    }
    const size_t tail_size = head->next->size;
    if (tail_size > overhead_size() + 2) {
        auto* consume_tail = static_cast<std::uint8_t*>(
            allocator.allocate(tail_size - (overhead_size() + 1)));
        if (!consume_tail || consume_tail == reinterpret_cast<void*>(-1)) {
            std::cout << "[FAIL] failed to consume tail for wrap-around setup\n";
            return false;
        }
    }

    // This request should start at prev_search_end->next, fail there, wrap to head, and allocate head.
    auto* wrapped = static_cast<std::uint8_t*>(allocator.allocate(head->size));
    if (!wrapped || wrapped == reinterpret_cast<void*>(-1)) {
        std::cout << "[FAIL] wrap-around allocation failed\n";
        return false;
    }

    auto trace2 = allocator.debug_last_allocate_trace();
    if (!trace2.started_from_prev_search_end) {
        std::cout << "[FAIL] allocation did not start from prev_search_end\n";
        return false;
    }
    if (!trace2.wrapped_to_head) {
        std::cout << "[FAIL] allocation did not wrap around to free-list head\n";
        return false;
    }
    if (trace2.selected_block != head) {
        std::cout << "[FAIL] wrap-around did not land on head block as expected\n";
        return false;
    }

    std::cout << "[PASS] prev_search_end traversal and wrap-around verified\n";
    return true;
}

} // namespace

int main() {
    struct TestCase {
        const char* name;
        bool (*fn)();
    };

    const std::vector<TestCase> tests = {
        {"return_back_reuses_freelist", test_return_back_reuses_freelist},
        {"no_new_block_when_free_space_exists", test_no_new_block_when_free_space_exists},
        {"coalescing_of_adjacent_freed_blocks", test_coalescing_of_adjacent_freed_blocks},
        {"pointer_arithmetic_and_boundary_safety", test_pointer_arithmetic_and_boundary_safety},
        {"stress_random_allocate_return_uniqueness", test_stress_random_allocate_return_uniqueness},
        {"prev_search_end_start_and_wraparound", test_prev_search_end_start_and_wraparound},
    };

    int passed = 0;
    for (const auto& test : tests) {
        std::cout << "\n========================================\n";
        std::cout << "Running " << test.name << "\n";
        const bool ok = test.fn();
        if (ok) {
            ++passed;
        }
    }

    std::cout << "\n========================================\n";
    std::cout << "Test Summary: " << passed << "/" << tests.size() << " tests passed\n";
    std::cout << "========================================\n";

    if (passed == static_cast<int>(tests.size())) {
        std::cout << "SUCCESS: All tests passed!\n";
        return 0;
    }
    std::cerr << "FAILURE: Some tests failed. See logs above.\n";
    return 1;
}