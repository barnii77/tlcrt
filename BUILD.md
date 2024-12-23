# Full build
Build the project with (1.) `cmake -DNO_MINOR_GC=OFF -B build` or just `cmake -B build` followed by (2.) `make -C build`.
The `build/` directory will then contain `libtlcrt.a` (linux) or similar (on other Operating Systems).
Notice that if you have previously built the project without minor GC (i.e. `-DNO_MINOR_GC=ON`), you must to use the first command from (1.).

# Build without minor GC
The tlcrt provides two types of garbage collection:
1. majorGC: The main garbage collector, a mark-and-sweep GC with support for incremental garbage collection.
    - Required: Should always be used, either on a separate thread (warning: requires manual synchronization with the interpreter) or directly.
2. minorGC: A second, optional reference counting-based garbage collector (without cycle detection).
    - Optional: May be used in addition to the majorGC to collect unused memory in simple cases immediately. The minorGC has unconditional overhead. Therefore, if it is not used, one should build this project without minorGC support by passing `-DNO_MINOR_GC=ON` to cmake. Details below.

To build without minor GC support, run `cmake -DNO_MINOR_GC=ON -B build`. Then proceed normally as described in section "Full build", step (2.).
Building this way will remove all minorGC overhead. The minorGC API does still exist, but any calls to it simply do nothing.
