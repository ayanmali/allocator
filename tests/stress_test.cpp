#include "/root/allocator/allocator.hpp"
#include <cstdio>

int main() {
    Allocator allocator;
    for (int i = 0; i < 200000; ++i) {
        int* p = static_cast<int*>(allocator.allocate(sizeof(int)));
        if (!p) {
            std::fprintf(stderr, "allocation failed at %d\n", i);
            return 1;
        }
        *p = i;
        if (*p != i) {
            std::fprintf(stderr, "write mismatch at %d\n", i);
            return 2;
        }
        allocator.deallocate(p);
    }
    std::puts("stress-ok");
    return 0;
}