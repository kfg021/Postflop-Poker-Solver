cmake_minimum_required(VERSION 3.14)

set(PROJECT_NAME PostflopSolver)
project(${PROJECT_NAME} VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenMP)

if(OpenMP_FOUND)
    message(STATUS "OpenMP found. Building with parallel support enabled.")
else()
    message(WARNING "OpenMP not found. Building in single-threaded mode.")
endif()

include(FetchContent)

FetchContent_Declare(
    replxx
    GIT_REPOSITORY https://github.com/AmokHuginnsson/replxx.git
    GIT_TAG release-0.0.4
)

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG yaml-cpp-0.9.0
)

FetchContent_MakeAvailable(replxx yaml-cpp)

add_library(postflop_solver_core STATIC
    src/game/game_utils.cpp
    src/game/kuhn_poker.cpp
    src/game/leduc_poker.cpp
    src/game/holdem/hand_evaluation.cpp
    src/game/holdem/holdem_parser.cpp
    src/game/holdem/holdem.cpp
    src/solver/cfr.cpp
    src/solver/tree.cpp
    src/util/scoped_timer.cpp
    src/util/string_utils.cpp
)

if(OpenMP_FOUND)
    if (MSVC)
        target_compile_options(postflop_solver_core PUBLIC /openmp:llvm)
    endif()

    target_link_libraries(postflop_solver_core PUBLIC OpenMP::OpenMP_CXX)
endif()

target_include_directories(postflop_solver_core PUBLIC include)

add_executable(${PROJECT_NAME}
    src/cli/cli_dispatcher.cpp
    src/cli/solver_commands.cpp
    src/main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    postflop_solver_core
    replxx::replxx
    yaml-cpp::yaml-cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME ${PROJECT_NAME}
)

option(BUILD_TESTING "Build tests" ON)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()