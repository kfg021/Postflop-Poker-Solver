cmake_minimum_required(VERSION 3.28)

set(PROJECT_NAME PostflopSolver)
project(${PROJECT_NAME} VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(postflop_solver_core STATIC
    src/game/game_utils.cpp
    src/game/kuhn_poker.cpp
    src/solver/cfr.cpp
    src/solver/tree.cpp
)

target_include_directories(postflop_solver_core PUBLIC include)

add_executable(${PROJECT_NAME}
    src/io/output.cpp
    src/trainer/train_kuhn_poker.cpp
    src/main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE postflop_solver_core)

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME ${PROJECT_NAME}
)