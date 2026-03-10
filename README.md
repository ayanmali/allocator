A memory allocator implementation in C++.

### TODO
- moving slowpath code (e.g. error handling) to non-inlined functions

- remove branches; replace w/ templates

- thread-local caches

- separate free lists based on size (i.e. different bins)

- freeing memory from a separate thread?

### Added
Block splitting and coalescing
Central free list - linked list of free blocks
returning blocks back to (the start of) the free list
  start of list vs based on address order; tradeoff between speed and fragmentation
next-fit allocation strategy
  uses a pointer to keep track of where the last search ended to avoid scanning unhelpful blocks
memory allocation via sbrk and mmap
sbrk and mmap-specific deallocation logic
boundary tags for easier pointer arithmetic to find the previous block and easier coalescing
