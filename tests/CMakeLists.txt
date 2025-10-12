cmake_minimum_required(VERSION 3.14)
project(postflop_solver_tests)

# GoogleTest requires at least C++17
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(run_tests
    hand_evaluation_tests.cpp
    holdem_action_tests.cpp
    input_parsing_tests.cpp
    end_to_end_tests.cpp
)

target_link_libraries(run_tests PRIVATE
    postflop_solver_core
    GTest::gtest
    GTest::gmock
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(run_tests)