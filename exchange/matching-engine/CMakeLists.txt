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
# "exchange/market-data/market-update.h".
set(REPO_ROOT ${PROJECT_SOURCE_DIR}/../..)

add_library(libcommon STATIC
    ${REPO_ROOT}/common/tcp-server.cpp
    ${REPO_ROOT}/common/tcp-socket.cpp
    ${REPO_ROOT}/common/mcast-socket.cpp)
target_include_directories(libcommon PUBLIC ${REPO_ROOT})

add_library(libexchange STATIC
    matcher/matching-engine.cpp
    matcher/me-order.cpp
    order-book/order-book.cpp
    ${REPO_ROOT}/exchange/order-server/order-server.cpp
    ${REPO_ROOT}/exchange/order-server/shm-server.cpp
    ${REPO_ROOT}/exchange/market-data/market-data-publisher.cpp
    ${REPO_ROOT}/exchange/market-data/snapshot-synthesizer.cpp)
target_link_libraries(libexchange PUBLIC libcommon)

add_executable(exchange_main matching-engine-main.cpp)
target_link_libraries(exchange_main PRIVATE libexchange pthread)

# The whole exchange: matching engine, order server, market data publisher.
add_executable(exchange ${REPO_ROOT}/exchange/exchange-main.cpp)
target_link_libraries(exchange PRIVATE libexchange pthread)

# Clients for a running exchange: TCP orders in, responses and multicast market data out.
add_executable(exchange_benchmark ${REPO_ROOT}/benchmarks/exchange-benchmark/exchange-benchmark.cpp)
target_link_libraries(exchange_benchmark PRIVATE libcommon pthread)

add_executable(matching_engine_benchmark ${REPO_ROOT}/benchmarks/matching-engine-benchmark/matching-engine-benchmark.cpp)
target_link_libraries(matching_engine_benchmark PRIVATE libexchange pthread)
