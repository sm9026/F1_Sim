cmake_minimum_required(VERSION 3.16)
project(F1_Sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(f1_sim_dal STATIC
    src/data_access/json_parser.cpp
    src/data_access/dataset_resolver.cpp
    src/data_access/data_access_layer.cpp
)

target_include_directories(f1_sim_dal
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/data_access
)

add_library(f1_sim_engine STATIC
    src/simulation/lap_evaluator.cpp
    src/simulation/tyre_inventory.cpp
    src/simulation/strategy_validator.cpp
    src/simulation/race_session.cpp
)

target_include_directories(f1_sim_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(f1_sim_engine PUBLIC f1_sim_dal)

add_executable(f1_sim
    src/main.cpp
)

target_link_libraries(f1_sim PRIVATE f1_sim_engine f1_sim_dal)

if(MSVC)
    target_compile_options(f1_sim_dal PRIVATE /W4)
    target_compile_options(f1_sim_engine PRIVATE /W4)
    target_compile_options(f1_sim PRIVATE /W4)
else()
    target_compile_options(f1_sim_dal PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(f1_sim_engine PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(f1_sim PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(f1_sim_dal_tests
    tests/data_access/dal_tests.cpp
)

target_link_libraries(f1_sim_dal_tests PRIVATE f1_sim_dal)

add_executable(f1_sim_simulation_tests
    tests/simulation/simulation_tests.cpp
)

target_link_libraries(f1_sim_simulation_tests PRIVATE f1_sim_engine f1_sim_dal)

if(MSVC)
    target_compile_options(f1_sim_dal_tests PRIVATE /W4)
    target_compile_options(f1_sim_simulation_tests PRIVATE /W4)
else()
    target_compile_options(f1_sim_dal_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(f1_sim_simulation_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME f1_sim_dal_tests
    COMMAND f1_sim_dal_tests "${CMAKE_CURRENT_SOURCE_DIR}/data"
)

add_test(
    NAME f1_sim_simulation_tests
    COMMAND f1_sim_simulation_tests "${CMAKE_CURRENT_SOURCE_DIR}/data"
)
