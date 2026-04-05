#include "allocator.hpp"
int main() {
    Allocator allocator = Allocator();
    void* idk = allocator.allocate(4);
    allocator.deallocate(idk);
    return 0;
}