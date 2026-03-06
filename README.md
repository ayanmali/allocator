A memory allocator implementation in C++.

TODO
- moving slowpath code (e.g. error handling) to non-inlined functions
- remove branches; replace w/ templates
- should newly returned blocks be inserted at the start of the list or based on address order?
    - tradeoff between speed and fragmentation
- thread-local caches
- separate free lists based on size (i.e. different bins)
- freeing memory from a separate thread?