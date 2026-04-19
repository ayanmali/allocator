#include "/root/allocator/allocator.hpp"
#include <cstdio>
int main() {
    Allocator allocator;
    int* p = static_cast<int*>(allocator.allocate(sizeof(int)));
    if (!p) return 1;
    *p = 32;
    std::printf("%p %d\n", static_cast<void*>(p), *p);
    allocator.deallocate(p);
    return 0;
}