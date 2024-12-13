#include <iostream>
#include <string>
#include <functional>
#include "tlc/rt.h"

bool all_tests_passed = true;

void runTest(const std::string& testName, const std::function<void()>& testFunction) {
    try {
        testFunction();
        std::cout << "[PASS] " << testName << "\n";
        return;
    } catch (const std::exception& ex) {
        std::cerr << "[FAIL] " << testName << ": " << ex.what() << "\n";
    } catch (...) {
        std::cerr << "[FAIL] " << testName << ": unknown exception\n";
    }
    all_tests_passed = false;
}

int main() {
    using namespace tlc::rt;

    Context ctx;

    runTest("Basic Allocation", [&]() {
        Value handle = ctx.alloc(10);
        if (handle.type != ValueType::memory_handle) {
            throw std::runtime_error("Handle type mismatch");
        }
    });

    runTest("Write and Read from MemoryHandle", [&]() {
        Value handle = ctx.alloc(5);
        ctx.write(handle, 0, Value(42, ValueType::integer));
        ctx.write(handle, 1, Value(99, ValueType::integer));
        Value val1 = ctx.read(handle, 0);
        Value val2 = ctx.read(handle, 1);
        if (val1.data != 42 || val2.data != 99) {
            throw std::runtime_error("Read/Write failed");
        }
    });

    runTest("Push and Pop MemoryHandle", [&]() {
        Value handle = ctx.alloc(0);
        ctx.push(handle, Value(1, ValueType::integer));
        ctx.push(handle, Value(2, ValueType::integer));
        Value val = ctx.pop(handle);
        if (val.data != 2) {
            throw std::runtime_error("Pop failed");
        }
    });

    runTest("Reference Counting via Variable Assignment", [&]() {
        VarT varID1 = 1;
        VarT varID2 = 2;
        Value handle = ctx.alloc(3);

        ctx.assign(varID1, handle); // Assign to varID1
        ctx.assign(varID2, handle); // Assign to varID2 (ref count increments)
        ctx.erase(varID1);          // Erase varID1 (ref count decrements)
        ctx.erase(varID2);          // Erase varID2 (ref count reaches 0, eligible for GC)
        ctx.minorGC();              // Perform GC to clean up
    });

    runTest("Overwriting MemoryHandle in Write Operation", [&]() {
        Value arrayHandle = ctx.alloc(2);
        Value subHandle = ctx.alloc(1);

        ctx.write(arrayHandle, 0, subHandle);            // Write subHandle to array
        ctx.write(arrayHandle, 0, Value(123, ValueType::integer)); // Overwrite with integer
        ctx.minorGC();                                  // subHandle should now be cleaned
    });

    runTest("Major Garbage Collection", [&]() {
        Value handle1 = ctx.alloc(2);
        Value handle2 = ctx.alloc(3);

        ctx.assign(1, handle1);  // Assign handle1 to a variable
        ctx.assign(2, handle2);  // Assign handle2 to a variable
        ctx.erase(1);            // Erase handle1
        ctx.majorGC();           // handle1 should be cleaned up
    });

    runTest("Binary Operator Addition", [&]() {
        Value v1(10, ValueType::integer);
        Value v2(20, ValueType::integer);
        Value result = v1 + v2;
        if (result.data != 30) {
            throw std::runtime_error("Addition operator failed");
        }
    });

    runTest("Binary Operator Multiplication", [&]() {
        Value v1(5, ValueType::integer);
        Value v2(6, ValueType::integer);
        Value result = v1 * v2;
        if (result.data != 30) {
            throw std::runtime_error("Multiplication operator failed, should have been 30, is " + std::to_string(result.data));
        }
    });

    runTest("Invalid Memory Access", [&]() {
        try {
            Value invalidHandle(999, ValueType::memory_handle);
            ctx.read(invalidHandle, 0);
        } catch (const std::runtime_error&) {
            // Expected
            return;
        }
        throw std::runtime_error("Invalid memory access did not throw");
    });

    runTest("Nested MemoryHandle with Multiple References", [&]() {
        // Allocate two memory handles
        Value outerHandle = ctx.alloc(2);
        Value innerHandle = ctx.alloc(3);

        // Write the inner handle into the outer handle
        ctx.write(outerHandle, 0, innerHandle);

        // Verify nested structure
        Value retrievedHandle = ctx.read(outerHandle, 0);
        if (retrievedHandle.data != innerHandle.data) {
            throw std::runtime_error("Nested handle retrieval failed");
        }

        // Modify inner handle's content
        ctx.write(innerHandle, 0, Value(123, ValueType::integer));
        Value innerValue = ctx.read(innerHandle, 0);
        if (innerValue.data != 123) {
            throw std::runtime_error("Inner handle modification failed");
        }

        // Clean up by erasing the outer handle reference
        ctx.assign(1, outerHandle);
        ctx.erase(1);
        ctx.minorGC(); // Both handles should now be cleaned up
    });

    runTest("Combined Arithmetic and Memory Operations", [&]() {
        // Allocate a memory handle and perform arithmetic operations on its content
        Value handle = ctx.alloc(3);
        ctx.write(handle, 0, Value(10, ValueType::integer));
        ctx.write(handle, 1, Value(20, ValueType::integer));
        ctx.write(handle, 2, Value(30, ValueType::integer));

        // Read and calculate sum
        Value val1 = ctx.read(handle, 0);
        Value val2 = ctx.read(handle, 1);
        Value val3 = ctx.read(handle, 2);
        Value sum = val1 + val2 + val3;

        if (sum.data != 60) {
            throw std::runtime_error("Arithmetic operations on memory handle contents failed");
        }

        // Clean up
        ctx.assign(1, handle);
        ctx.erase(1);
        ctx.minorGC();
    });

    runTest("Chain of Assignments and MemoryHandle GC", [&]() {
        Value handle = ctx.alloc(1);

        // Create a chain of variable assignments
        ctx.assign(1, handle);
        ctx.assign(2, handle);
        ctx.assign(3, handle);

        // Erase some variables
        ctx.erase(1);
        ctx.erase(2);

        // Ensure the memory handle is still valid (not GC'ed)
        Value stillValid = ctx.read(handle, 0);
        if (handle.type != ValueType::memory_handle) {
            throw std::runtime_error("Handle was prematurely GC'ed");
        }

        // Erase the last reference and trigger GC
        ctx.erase(3);
        ctx.minorGC();

        // Accessing the handle now should throw
        try {
            ctx.read(handle, 0);
        } catch (const std::runtime_error&) {
            // Expected behavior
            return;
        }
        throw std::runtime_error("Expected exception for GC'ed handle");
    });

    for (int n_magc_runs : {1, 2, 3})
        runTest(std::string("Variable Reassignment and Memory Reuse - {'n_magc_runs': ")
                + std::to_string(n_magc_runs)
                + "}",
                [&]() {
            // Assign one memory handle to two variables
            Value handle1 = ctx.alloc(5);
            ctx.assign(1, handle1);
            ctx.assign(2, handle1);

            // Erase one variable
            ctx.erase(1);

            // Reassign the remaining variable to a new handle
            Value handle2 = ctx.alloc(10);
            ctx.assign(2, handle2);

            // Ensure the first handle is not prematurely GC'ed
            try {
                ctx.read(handle1, 0);
            } catch (const std::runtime_error&) {
                throw std::runtime_error("Handle was GC'ed despite existing reference");
            }

            // Erase the second variable and trigger GC
            ctx.erase(2);
            while (n_magc_runs--)
                ctx.minorGC();

            // Now the first handle should be cleaned
            try {
                ctx.read(handle1, 0);
            } catch (const std::runtime_error&) {
                // Expected behavior
                return;
            }
            throw std::runtime_error("Handle was not GC'ed after all references were erased");
        });

    for (bool make_complex_structure : {false, true})
    for (int gc_cleanup_size : {-1, 10})
    for (int n_magc_runs : {1, 2, 3}) {
        runTest(std::string("Garbage Collection with Cyclic References - {'gc_cleanup_size': ")
                + std::to_string(gc_cleanup_size)
                + ", 'n_magc_runs': "
                + std::to_string(n_magc_runs)
                + ", 'make_complex_structure': "
                + std::to_string(make_complex_structure)
                + "}",
                [&]() {
            Value handleA;
            Value handleB;
            Value handleC;
            Value handleE;
            if (make_complex_structure) {
                handleA = ctx.alloc(2);
                handleB = ctx.alloc(2);
                handleC = ctx.alloc(1);
                Value handleD = ctx.alloc(1);
                handleE = ctx.alloc(1);
                ctx.write(handleA, 1, handleC);
                ctx.write(handleC, 0, handleD);
                ctx.write(handleD, 0, handleE);
                ctx.write(handleE, 0, handleB);
                ctx.write(handleB, 1, handleE);
            } else {
                handleA = ctx.alloc(1);
                handleB = ctx.alloc(1);
            }

            // Create cyclic references
            ctx.write(handleA, 0, handleB);
            ctx.write(handleB, 0, handleA);

            // Assign handles to variables
            ctx.assign(1, handleA);
            ctx.assign(2, handleB);

            // Perform GC while the values are still reachable
            int n_magc_runs_tmp = n_magc_runs;
            while (n_magc_runs_tmp--)
                ctx.minorGC();
            n_magc_runs_tmp = n_magc_runs;
            while (n_magc_runs_tmp--)
                ctx.majorGC(gc_cleanup_size);
            Value checkA = ctx.read(handleA, 0);
            Value checkB = ctx.read(handleB, 0);
            if (checkA.data != handleB.data || checkB.data != handleA.data) {
                throw std::runtime_error("Cyclic references were incorrectly cleaned while still reachable");
            }
            if (make_complex_structure) {
                checkA = ctx.read(handleA, 1);
                checkB = ctx.read(handleB, 1);
                if (checkA.data != handleC.data || checkB.data != handleE.data) {
                    throw std::runtime_error("Complex structure was incorrectly cleaned while still reachable");
                }
            }

            // Erase all variables, leaving the cycle
            ctx.erase(1);
            ctx.erase(2);

            // Perform a minor GC (cycle should still exist)
            ctx.minorGC();

            // Verify handles are still valid
            checkA = ctx.read(handleA, 0);
            checkB = ctx.read(handleB, 0);
            if (checkA.data != handleB.data || checkB.data != handleA.data) {
                throw std::runtime_error("Cyclic references were incorrectly cleaned by minor GC");
            }
            if (make_complex_structure) {
                checkA = ctx.read(handleA, 1);
                checkB = ctx.read(handleB, 1);
                if (checkA.data != handleC.data || checkB.data != handleE.data) {
                    throw std::runtime_error("Complex structure was incorrectly cleaned while still reachable");
                }
            }

            // Perform a major GC to clean the cycle
            while (n_magc_runs--)
                ctx.majorGC(gc_cleanup_size);
            try {
                ctx.read(handleA, 0);
            } catch (const std::runtime_error& e) {
                // Expected behavior
                return;
            }
            throw std::runtime_error("Cyclic references were not cleaned by major GC");
        });
    }

    runTest("Insufficient Iteration Garbage Collection with Cyclic References", [&]() {
        int gc_cleanup_size = 1;

        Value handleA = ctx.alloc(1);
        Value handleB = ctx.alloc(1);

        // Create cyclic references
        ctx.write(handleA, 0, handleB);
        ctx.write(handleB, 0, handleA);

        // Assign handles to variables
        ctx.assign(1, handleA);
        ctx.assign(2, handleB);

        // Perform GC while the values are still reachable
        ctx.minorGC();
        ctx.majorGC(gc_cleanup_size);
        Value checkA = ctx.read(handleA, 0);
        Value checkB = ctx.read(handleB, 0);
        if (checkA.data != handleB.data || checkB.data != handleA.data) {
            throw std::runtime_error("Cyclic references were incorrectly cleaned while still reachable");
        }

        // Erase all variables, leaving the cycle
        ctx.erase(1);
        ctx.erase(2);

        // Perform a minor GC (cycle should still exist)
        ctx.minorGC();

        // Verify handles are still valid
        checkA = ctx.read(handleA, 0);
        checkB = ctx.read(handleB, 0);
        if (checkA.data != handleB.data || checkB.data != handleA.data) {
            throw std::runtime_error("Cyclic references were incorrectly cleaned by minor GC");
        }

        // Perform a major GC with insufficient iteration limit to make sure it doesn't exceed it's limit and fails to clean up
        ctx.majorGC(gc_cleanup_size);
        try {
            ctx.read(handleA, 0);
        } catch (const std::runtime_error& e) {
            throw std::runtime_error("Cyclic references were cleaned by major GC but should not have been");
        }
        // Expected behavior (now clean up for real)
        ctx.majorGC();
    });

    for (bool make_complex_structure : {false, true})
        runTest(std::string("Multi-Iteration Garbage Collection with Intermediate Work - {'make_complex_structure': ")
                + std::to_string(make_complex_structure)
                + "}",
                [&]() {
            int n_magc_runs = 5;
            int gc_cleanup_size = 2;

            Value handleA;
            Value handleB;
            Value handleC;
            Value handleE;
            if (make_complex_structure) {
                handleA = ctx.alloc(2);
                handleB = ctx.alloc(2);
            } else {
                handleA = ctx.alloc(1);
                handleB = ctx.alloc(1);
            }

            // Create cyclic references
            ctx.write(handleA, 0, handleB);
            ctx.write(handleB, 0, handleA);

            // Assign handles to variables
            ctx.assign(1, handleA);
            ctx.assign(2, handleB);

            // Perform GC while the values are still reachable
            ctx.minorGC();
            int n_magc_runs_tmp = n_magc_runs;
            while (n_magc_runs_tmp--)
                ctx.majorGC(gc_cleanup_size);
            Value checkA = ctx.read(handleA, 0);
            Value checkB = ctx.read(handleB, 0);
            if (checkA.data != handleB.data || checkB.data != handleA.data) {
                throw std::runtime_error("Cyclic references were incorrectly cleaned while still reachable");
            }
            if (make_complex_structure) {
                handleC = ctx.alloc(1);
                Value handleD = ctx.alloc(1);
                handleE = ctx.alloc(1);
                ctx.write(handleA, 1, handleC);
                ctx.write(handleC, 0, handleD);
                ctx.write(handleD, 0, handleE);
                ctx.write(handleE, 0, handleB);
                ctx.write(handleB, 1, handleE);

                checkA = ctx.read(handleA, 1);
                checkB = ctx.read(handleB, 1);
                if (checkA.data != handleC.data || checkB.data != handleE.data) {
                    throw std::runtime_error("Complex structure was incorrectly cleaned while still reachable");
                }
            }

            // Erase all variables, leaving the cycle
            ctx.erase(1);
            ctx.erase(2);

            // Perform a minor GC (cycle should still exist)
            ctx.minorGC();

            // Verify handles are still valid
            checkA = ctx.read(handleA, 0);
            checkB = ctx.read(handleB, 0);
            if (checkA.data != handleB.data || checkB.data != handleA.data) {
                throw std::runtime_error("Cyclic references were incorrectly cleaned by minor GC");
            }
            if (make_complex_structure) {
                checkA = ctx.read(handleA, 1);
                checkB = ctx.read(handleB, 1);
                if (checkA.data != handleC.data || checkB.data != handleE.data) {
                    throw std::runtime_error("Complex structure was incorrectly cleaned while still reachable");
                }
            }

            while (n_magc_runs--)
                ctx.majorGC(gc_cleanup_size);
            try {
                ctx.read(handleA, 0);
            } catch (const std::runtime_error& e) {
                // Expected behavior
                return;
            }
            throw std::runtime_error("Cyclic references were not cleaned by major GC");
        });

    std::cout << (all_tests_passed ? "All tests passed!" : "Some tests failed!") << "\n";
    return 0;
}

