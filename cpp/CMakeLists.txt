cmake_minimum_required(VERSION 3.20)
project(quant-engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ENGINE_SOURCES
    core/symbol_table.cpp
    core/context.cpp
    core/engine.cpp
    core/sim_broker.cpp
    core/portfolio.cpp
    data/csv_feed.cpp
    indicator/sma.cpp
    indicator/ema.cpp
    indicator/rsi.cpp
    indicator/macd.cpp
    indicator/bollinger.cpp
    analyzer/performance.cpp
)

add_library(qe_core STATIC ${ENGINE_SOURCES})
target_include_directories(qe_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(qe_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Main executable for testing
add_executable(qe_backtest main.cpp strategies/dual_ma.cpp)
target_link_libraries(qe_backtest PRIVATE qe_core)

# Unit tests
add_executable(qe_test test/test_main.cpp)
target_link_libraries(qe_test PRIVATE qe_core)

# --- pybind11 Python module ---
include(FetchContent)
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.13.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(pybind11)

pybind11_add_module(qe bind/pybind_module.cpp)
target_link_libraries(qe PRIVATE qe_core)

# Benchmark
add_executable(qe_bench bench/benchmark.cpp)
target_link_libraries(qe_bench PRIVATE qe_core)
target_compile_options(qe_bench PRIVATE -O2)
