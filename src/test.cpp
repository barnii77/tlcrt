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
            throw std::runtime_error("Invalid memory access did not throw");
        } catch (const std::runtime_error&) {
            // Expected
        }
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
            throw std::runtime_error("Expected exception for GC'ed handle");
        } catch (const std::runtime_error&) {
            // Expected behavior
        }
    });

    runTest("Garbage Collection with Cyclic References", [&]() {
        Value handleA = ctx.alloc(1);
        Value handleB = ctx.alloc(1);

        // Create cyclic references
        ctx.write(handleA, 0, handleB);
        ctx.write(handleB, 0, handleA);

        // Assign handles to variables
        ctx.assign(1, handleA);
        ctx.assign(2, handleB);

        // Erase all variables, leaving the cycle
        ctx.erase(1);
        ctx.erase(2);

        // Perform a minor GC (cycle should still exist)
        ctx.minorGC();

        // Verify handles are still valid
        Value checkA = ctx.read(handleA, 0);
        Value checkB = ctx.read(handleB, 0);
        if (checkA.data != handleB.data || checkB.data != handleA.data) {
            throw std::runtime_error("Cyclic references were incorrectly cleaned by minor GC");
        }

        // Perform a major GC to clean the cycle
        ctx.majorGC();
        try {
            ctx.read(handleA, 0);
            throw std::runtime_error("Cyclic references were not cleaned by major GC");
        } catch (const std::runtime_error&) {
            // Expected behavior
        }
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

    runTest("Variable Reassignment and Memory Reuse", [&]() {
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
        ctx.minorGC();

        // Now the first handle should be cleaned
        try {
            ctx.read(handle1, 0);
            throw std::runtime_error("Handle was not GC'ed after all references were erased");
        } catch (const std::runtime_error&) {
            // Expected behavior
        }
    });

    std::cout << (all_tests_passed ? "All tests passed!" : "Some tests failed!") << "\n";
    return 0;
}

