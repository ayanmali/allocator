#include "allocator.hpp"
#include <iostream>

void custom_alloc(Allocator& allocator) {
    int* idk = (int*) allocator.allocate(4);
    //void* idk = allocator.allocate(4);
    //*idk = 32;
    std::cout << idk << "\n";

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
