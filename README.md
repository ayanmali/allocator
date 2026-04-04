A memory allocator implementation in C++ inspired by the TCMalloc design.

### TODO
- implement per-CPU caches w/ rseq syscall

- dynamic resizing of transfer cache capacity (number of batches each size class's transfer cache can hold), capacity stealing across size classes

- function inlining/no inlining

- locking on the page heap, central free lists

- replace page map with a radix tree or cache friendly hash table

- NUMA-awareness

- update struct member ordering and field access patterns

- replace Span.total_count and Span.allocated_count with a bitmask instead of two ints

- copy/move ctors

- moving slowpath code (e.g. error handling) to non-inlined functions

- remove branches; replace w/ templates

- use `madvise` with `MADV_DONTNEED` and/or `MADV_FREE`

- better security - protection from buffer overflows, etc

- freeing memory from a separate thread?

### Added
Span splitting and coalescing

Central free lists - linked lists of free blocks, organized by size class (see `config.hpp`)

Returning blocks back to (the start of) the free list
  start of list vs based on address order; tradeoff between speed and fragmentation

Each Span maintains its own free list to allow for releasing of Spans back to the page heap in O(1) 

Each CFL uses a pointer to keep track of a Span with free objects to allow for O(1) allocation from the page heap

memory allocation via sbrk and mmap

- implement SoA pattern instead of AoS - per-CPU caches, transfer caches, CFLs

- storing 16 bit free object indexes inside spans to pack indexes together
  - unrolled linked list --> handles tiny allocations

- lightweight locking on transfer caches

- batch transfers between frontend slabs and transfer caches/CFLs