cmake_minimum_required(VERSION 3.28)

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
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(postflop_solver_core STATIC
    src/game/game_utils.cpp
    src/game/kuhn_poker.cpp
    src/game/leduc_poker.cpp
    src/game/holdem/hand_evaluation.cpp
    src/game/holdem/holdem.cpp
    src/game/holdem/parse_input.cpp
    src/solver/cfr.cpp
    src/solver/tree.cpp
)

if(OpenMP_FOUND)
    target_link_libraries(postflop_solver_core PUBLIC OpenMP::OpenMP_CXX)
endif()

target_include_directories(postflop_solver_core PUBLIC include)

add_executable(${PROJECT_NAME}
    src/trainer/output.cpp
    src/trainer/train.cpp
    src/util/size_string.cpp
    src/main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    postflop_solver_core
    nlohmann_json::nlohmann_json
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME ${PROJECT_NAME}
)

option(BUILD_TESTING "Build tests" ON)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()