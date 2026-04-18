#include "allocator.hpp"
#include <iostream>

void custom_alloc(Allocator& allocator) {
    int* idk = (int*) allocator.allocate(4 * sizeof(int));
    //void* idk = allocator.allocate(4);
    for (int i = 0; i < 4; ++i) {
        idk[i] = i;
    }
    for (int j = 0; j < 4; ++j) {
        std::cout << idk[j] << "\n";
    }

    //std::cout << idk << "\n";
    allocator.deallocate(idk);

}

void default_alloc() {
    void* idk = malloc(4);
    free(idk);
}

int main() {
    Allocator allocator = Allocator();
    custom_alloc(allocator);
    //default_alloc();
    return 0;
}
