A memory allocator implementation in C++.

TODO
- moving slowpath code (e.g. error handling) to non-inlined functions
- free list should only contain free blocks; need to implement linking/unlinking logic
    - should newly freed blocks be inserted at the start of the list or based on address order?
        - tradeoff between speed and fragmentation
- coalescing adjacent free blocks
- thread-local caches
- separate free lists based on size (i.e. different bins)
- freeing memory from a separate thread?