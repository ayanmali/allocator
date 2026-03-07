#ifndef TESTS
#define TESTS
#include "allocator.hpp"

inline bool test1() {
    Allocator allocator{};
    int* n = (int*) allocator.allocate(100 * sizeof(int));

    //std::cout << "checking OOB: " << n[100] << "\n";

    char* s = (char*) allocator.allocate(50 * sizeof(char));

    // testing
    for (int i = 0; i < 100; ++i) {
        n[i] = i;
    }
    for (int j = 0; j < 100; ++j) {
        std::cout << n[j] << "\n";
    }

    bool ok = allocator.deallocate(n);
    if (!ok) {
        std::cout << "error deallocating memory - int\n";
        return false;
    }

    std::string idk = "abcdefghijklmnopqrstuvwxyz";
    for (int k = 0; k < 50; ++k) {
        s[k] = idk[k % idk.size()];
    }

    for (int m = 0; m < 50; ++m) {
        std::cout << s[m] << "\n";
    }
    
    ok = allocator.deallocate(s);
    if (!ok) {
        std::cout << "error deallocating memory - char\n";
        return false;
    }

    return true;

} 

inline bool test2() {
    Allocator allocator{};
    int* n = (int*) allocator.allocate(100 * sizeof(int));

    // testing
    for (int i = 0; i < 100; ++i) {
        n[i] = i;
    }
    for (int j = 0; j < 100; ++j) {
        std::cout << n[j] << "\n";
    }

    bool ok = allocator.return_back(n);

    char* s = (char*) allocator.allocate(50 * sizeof(char));
    
    std::string idk = "abcdefghijklmnopqrstuvwxyz";
    for (int k = 0; k < 50; ++k) {
        s[k] = idk[k % idk.size()];
    }

    for (int m = 0; m < 50; ++m) {
        std::cout << s[m] << "\n";
    }
    
    ok = allocator.deallocate(s);
    if (!ok) {
        std::cout << "error deallocating memory - char\n";
        return false;
    }

    return true;

} 

#endif