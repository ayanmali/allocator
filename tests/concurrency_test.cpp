#include "/root/allocator/allocator.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

namespace {

struct LiveAllocation {
    void* ptr;
    std::size_t size;
    std::uint64_t tag;
};

constexpr std::size_t kLiveSetPerThread = 256;
constexpr std::size_t kOpsPerThread = 50000;
constexpr std::size_t kSizes[] = {
    8, 16, 24, 32, 48, 64, 80, 96,
    112, 128, 160, 176, 208, 256, 312, 384,
    448, 512, 704, 896, 1024, 1408, 2048, 4096
};

std::uint8_t pattern_byte(std::uint64_t tag, std::size_t offset) {
    return static_cast<std::uint8_t>((tag * 1315423911ULL + offset * 17ULL) & 0xffU);
}

void fill_allocation(const LiveAllocation& allocation) {
    auto* bytes = static_cast<std::uint8_t*>(allocation.ptr);
    for (std::size_t i = 0; i < allocation.size; ++i) {
        bytes[i] = pattern_byte(allocation.tag, i);
    }
}

bool verify_allocation(const LiveAllocation& allocation) {
    auto* bytes = static_cast<std::uint8_t*>(allocation.ptr);
    for (std::size_t i = 0; i < allocation.size; ++i) {
        if (bytes[i] != pattern_byte(allocation.tag, i)) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    Allocator allocator;
    const unsigned int hw_threads = std::thread::hardware_concurrency();
    const std::size_t thread_count =
        std::max<std::size_t>(4, std::min<std::size_t>(8, hw_threads == 0 ? 4 : hw_threads));

    std::atomic<bool> failed{false};
    std::atomic<std::size_t> allocations{0};
    std::atomic<std::size_t> deallocations{0};
    std::atomic<std::size_t> allocation_failures{0};
    std::atomic<std::size_t> verification_failures{0};

    std::vector<std::thread> workers;
    workers.reserve(thread_count);

    for (std::size_t thread_id = 0; thread_id < thread_count; ++thread_id) {
        workers.emplace_back([&, thread_id]() {
            std::minstd_rand rng(static_cast<unsigned int>(thread_id + 1U) * 48271U);
            std::uniform_int_distribution<std::size_t> size_dist(0, std::size(kSizes) - 1);
            std::uniform_int_distribution<int> action_dist(0, 99);
            std::vector<LiveAllocation> live;
            live.reserve(kLiveSetPerThread);

            auto release_one = [&](std::size_t index) {
                LiveAllocation allocation = live[index];
                if (!verify_allocation(allocation)) {
                    verification_failures.fetch_add(1, std::memory_order_relaxed);
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
                allocator.deallocate(allocation.ptr);
                deallocations.fetch_add(1, std::memory_order_relaxed);
                live[index] = live.back();
                live.pop_back();
            };

            for (std::size_t op = 0; op < kOpsPerThread && !failed.load(std::memory_order_relaxed); ++op) {
                const bool should_allocate =
                    live.empty() ||
                    (live.size() < kLiveSetPerThread && action_dist(rng) < 60);

                if (should_allocate) {
                    const std::size_t size = kSizes[size_dist(rng)];
                    void* ptr = allocator.allocate(size);
                    if (!ptr) {
                        allocation_failures.fetch_add(1, std::memory_order_relaxed);
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }

                    LiveAllocation allocation{
                        ptr,
                        size,
                        (static_cast<std::uint64_t>(thread_id) << 32) | op
                    };
                    fill_allocation(allocation);
                    live.push_back(allocation);
                    allocations.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::uniform_int_distribution<std::size_t> live_dist(0, live.size() - 1);
                    release_one(live_dist(rng));
                }
            }

            while (!live.empty() && !failed.load(std::memory_order_relaxed)) {
                release_one(live.size() - 1);
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    if (failed.load(std::memory_order_relaxed)) {
        std::fprintf(stderr,
                     "multithreaded stress test failed: allocation_failures=%zu verification_failures=%zu allocs=%zu frees=%zu\n",
                     allocation_failures.load(std::memory_order_relaxed),
                     verification_failures.load(std::memory_order_relaxed),
                     allocations.load(std::memory_order_relaxed),
                     deallocations.load(std::memory_order_relaxed));
        return 1;
    }

    std::printf("stress-ok threads=%zu allocs=%zu frees=%zu\n",
                thread_count,
                allocations.load(std::memory_order_relaxed),
                deallocations.load(std::memory_order_relaxed));
    return allocations.load(std::memory_order_relaxed) ==
                   deallocations.load(std::memory_order_relaxed)
               ? 0
               : 2;
}