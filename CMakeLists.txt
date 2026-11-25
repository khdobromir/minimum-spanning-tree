cmake_minimum_required(VERSION 3.30)
project(minimum_spanning_tree VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MST_FULL_CPP26 "Enable available experimental C++26 language features" OFF)
option(MST_ASAN "Enable AddressSanitizer" OFF)
option(MST_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
include(cmake/FeatureDetection.cmake)
include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)

add_library(mst_core src/core/error.cpp)
target_include_directories(mst_core PUBLIC include "${PROJECT_BINARY_DIR}/generated")
target_compile_features(mst_core PUBLIC cxx_std_26)
target_link_libraries(mst_core PUBLIC mst_sanitizers PRIVATE mst_warnings)
target_compile_options(mst_core PUBLIC ${MST_EXPERIMENTAL_FLAGS})
target_link_options(mst_core PUBLIC ${MST_EXPERIMENTAL_LINK_FLAGS})

add_library(mst_benchmark src/benchmark/benchmark.cpp)
target_link_libraries(mst_benchmark PUBLIC mst_core PRIVATE mst_warnings)

add_executable(mst src/app/main.cpp)
target_link_libraries(mst PRIVATE mst_core mst_benchmark mst_warnings)

include(CTest)
if(BUILD_TESTING)
    find_package(Catch2 3.11.0 EXACT CONFIG QUIET)
    if(NOT Catch2_FOUND)
        include(FetchContent)
        FetchContent_Declare(Catch2
            URL https://codeload.github.com/catchorg/Catch2/tar.gz/refs/tags/v3.11.0
            URL_HASH SHA256=82fa1cb59dc28bab220935923f7469b997b259eb192fb9355db62da03c2a3137
        )
        FetchContent_MakeAvailable(Catch2)
        list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    endif()
    add_executable(mst_tests tests/unit/core.cpp tests/property/random.cpp)
    target_link_libraries(mst_tests PRIVATE mst_benchmark Catch2::Catch2WithMain mst_warnings)
    target_compile_definitions(mst_tests PRIVATE MST_EXAMPLES="${PROJECT_SOURCE_DIR}/examples")
    include(Catch)
    catch_discover_tests(mst_tests DISCOVERY_MODE PRE_TEST)
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/cli-test")
    add_test(NAME cli COMMAND "${CMAKE_COMMAND}" "-DMST=$<TARGET_FILE:mst>"
        "-DEXAMPLES=${PROJECT_SOURCE_DIR}/examples" "-DWORK=${PROJECT_BINARY_DIR}/cli-test"
        -P "${PROJECT_SOURCE_DIR}/tests/integration/cli.cmake")
endif()
