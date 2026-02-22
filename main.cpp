#include <cstddef>
#include <cstdio>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

struct Allocator {
    void* allocate(size_t size) {
        // int fd = open("/dev/zero", O_RDWR);
        // if (fd == -1) {
        //     std::cerr << "Failed to open device";
        // }
        void* mem = (void*)mmap(
            // TODO: consider MAP_NORESERVE, MAP_HUGETLB, MAP_LOCKED, MAP_POPULATE
            nullptr, size, 
            PROT_READ | PROT_WRITE, 
            MAP_ANONYMOUS | MAP_PRIVATE,
             -1, 0);
        return mem;
    }

    template <typename T>
    int deallocate(T* ptr, size_t size) {
        int ok = munmap(ptr, size);
        return ok;
    }
};

int main() {
    Allocator allocator{};
    int* n = static_cast<int*>(allocator.allocate(100));
    allocator.deallocate(n, 100);
    return 0;
}