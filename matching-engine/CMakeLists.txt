cmake_minimum_required(VERSION 3.16)

project(LowLatencyApp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Default to an optimized build (-O3 -DNDEBUG) so benchmarks are comparable.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -Wpedantic")
set(CMAKE_VERBOSE_MAKEFILE on)

# Headers are included relative to the repo root, e.g. "common/types.h" and
# "matching-engine/market-data/market-update.h".
set(REPO_ROOT ${PROJECT_SOURCE_DIR}/..)

add_library(libcommon STATIC
    ${REPO_ROOT}/common/tcp-server.cpp
    ${REPO_ROOT}/common/tcp-socket.cpp)
target_include_directories(libcommon PUBLIC ${REPO_ROOT})

add_library(libexchange STATIC
    matcher/matching-engine.cpp
    matcher/me-order.cpp
    order-book/order-book.cpp)
target_link_libraries(libexchange PUBLIC libcommon)

add_executable(exchange_main matching-engine-main.cpp)
target_link_libraries(exchange_main PRIVATE libexchange pthread)

add_executable(matching_engine_benchmark ${REPO_ROOT}/benchmarks/matching-engine-benchmark.cpp)
target_link_libraries(matching_engine_benchmark PRIVATE libexchange pthread)
