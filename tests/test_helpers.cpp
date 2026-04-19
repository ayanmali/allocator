// #include "size_classes.hpp"
// #include <iostream>
// #include <chrono>

// /*
// Testing whether linear search or binary search runs faster.
// */
// static constexpr int ITERATIONS = 10'000'000;

// int main() {
//     volatile size_t in = 1500;
//     volatile uint32_t sink = 0;

//     auto start1 = std::chrono::high_resolution_clock::now();
//     for (int i = 0; i < ITERATIONS; ++i)
//         sink = round_size_class(in).size;
//     auto end1 = std::chrono::high_resolution_clock::now();
//     auto ns1 = std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1).count();
//     std::cout << "Binary search - " << ns1 / ITERATIONS << " ns/call"
//               << " (" << ns1 << " ns total)\n";

//     auto start2 = std::chrono::high_resolution_clock::now();
//     for (int i = 0; i < ITERATIONS; ++i)
//         sink = round_size_class_linear(in).size;
//     auto end2 = std::chrono::high_resolution_clock::now();
//     auto ns2 = std::chrono::duration_cast<std::chrono::nanoseconds>(end2 - start2).count();
//     std::cout << "Linear search - " << ns2 / ITERATIONS << " ns/call"
//               << " (" << ns2 << " ns total)\n";

//     (void)sink;
//     return 0;
// }