#pragma once
#include <cstdint>
#include "time-utils.h"

namespace Common{
    // Read the TSC register: CPU clock cycles since reset.
    // constant_tsc/nonstop_tsc CPUs tick at a fixed rate regardless of frequency scaling,
    // so cycles / TSC GHz = nanoseconds.
    inline auto rdtsc() noexcept {
        unsigned int lo, hi;
        __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }
}

// LOGGER is a Logger (not a pointer).
// Log lines are "<epoch nanos> <RDTSC|TTT> <TAG> <value>" and parsed by scripts/perf-analysis.py.
// The timestamp is pushed as an integer, the logger thread formats it.

// Start measuring a code block. Creates a variable called TAG in the local scope.
#define START_MEASURE(TAG) const auto TAG = Common::rdtsc()

// End measuring a code block started with START_MEASURE(TAG), logs elapsed cycles.
#define END_MEASURE(TAG, LOGGER)                                                                \
    do {                                                                                        \
        const auto end = Common::rdtsc();                                                       \
        LOGGER.log("% RDTSC " #TAG " %\n", Common::getCurrentNanos(), (end - TAG)); \
    } while(false)

// Time-to-tick: log the current nanosecond timestamp at a hop on the order's path.
// The difference between two hops' timestamps is the latency between them.
#define TTT_MEASURE(TAG, LOGGER)                                                                \
    do {                                                                                        \
        const auto TAG = Common::getCurrentNanos();                                             \
        LOGGER.log("% TTT " #TAG " %\n", TAG, TAG);                                         \
    } while(false)
