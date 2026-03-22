A memory allocator implementation in C++.

### TODO
- Batch Transfer in the central free lists (num_to_move)
  The sc_info.num_to_move field specifies how many objects to move at once between a thread cache and the CFL. This is a future optimization for when thread caches are added -- the CFL would expose fetch_batch(FreeObject** list, uint32_t count) and return_batch(FreeObject* list, uint32_t count) methods.

- locking on the page heap

- replace page map with a radix tree or cache friendly hash table

- update struct member ordering

- replace Span.total_count and Span.allocated_count with a bitmask instead of two ints

- copy/move ctors

- how to handle small allocations?

- moving slowpath code (e.g. error handling) to non-inlined functions

- remove branches; replace w/ templates

- use `madvise` with `MADV_DONTNEED` and/or `MADV_FREE`

- thread-local/CPU caches

- freeing memory from a separate thread?

### Added
Span splitting and coalescing
Central free lists - linked lists of free blocks, organized by size class (see `config.hpp`)
Returning blocks back to (the start of) the free list
  start of list vs based on address order; tradeoff between speed and fragmentation
Each Span maintains its own free list to allow for releasing of Spans back to the page heap in O(1) 
Each CFL uses a pointer to keep track of a Span with free objects to allow for O(1) allocation from the page heap
memory allocation via sbrk and mmap
sbrk and mmap-specific deallocation logic