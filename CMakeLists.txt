cmake_minimum_required(VERSION 3.14)

set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")

project(
    tlc
    VERSION 0.1.0
    DESCRIPTION "the tlc (typeless caml) programming language runtime"
    HOMEPAGE_URL "https://github.com/barnii77/tlc"
    LANGUAGES CXX
)

option(NO_MINOR_GC "Makes the runtime compile without minorGC such that minorGC invokations will return immediately, eliminating reference counting overhead. Recommended if minorGC is unused." OFF)

if(NO_MINOR_GC)
    message("Disabling minorGC")
    add_compile_definitions(NO_MINOR_GC)
endif()

include_directories(BEFORE include)

add_library(
    tlcrt
    lib/rt.cpp
    lib/value.cpp
)
target_compile_definitions(tlcrt PRIVATE $<$<CONFIG:Release>:_RELEASE_BUILD>)
target_compile_features(tlcrt PRIVATE cxx_std_17)

add_executable(tlc_tests src/test.cpp)
target_compile_features(tlc_tests PRIVATE cxx_std_17)
target_link_libraries(tlc_tests PRIVATE tlcrt)
