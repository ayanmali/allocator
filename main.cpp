#include "allocator.hpp"
#include "tests.hpp"
#include <functional>

int main() {
    Allocator allocator{};
    uint num_tests_pass = 0;
    bool ok;
    auto tests = std::vector<std::function<bool()>>{
        test1, 
        test2
    };

    for (uint i = 0; i < tests.size(); ++i) {
        std::cout << "RUNNING TEST " << i << "\n";
        ok = tests[i]();
        if (!ok) {
            std::cerr << "ERROR IN TEST " << i << "\n";
            continue;
        }
        num_tests_pass++;
        std::cout << "TEST " << i << " PASS\n";
    }
   
    std::cout << "TESTS PASSED: " << num_tests_pass << "/" << tests.size() << "\n";
    if (num_tests_pass == tests.size()) {
        std::cout << "ALL TESTS PASS\n";
    }
    return 0;
}