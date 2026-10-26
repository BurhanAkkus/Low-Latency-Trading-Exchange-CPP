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

    // rdtsc doesn't wait for earlier instructions and later ones can run before it, so the out
    // of order core blurs a measured block's edges. lfence starts nothing until everything before
    // it has finished. Neither waits for stores to drain from the store buffer.
    // Start of a block: read after earlier code has finished, before the block starts.
    inline auto rdtscStart() noexcept {
        unsigned int lo, hi;
        __asm__ __volatile__ ("lfence\n\trdtsc\n\tlfence" : "=a" (lo), "=d" (hi) :: "memory");
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }
    // End of a block: rdtscp waits for the block's instructions, lfence keeps later code out.
    inline auto rdtscEnd() noexcept {
        unsigned int lo, hi;
        __asm__ __volatile__ ("rdtscp\n\tlfence" : "=a" (lo), "=d" (hi) :: "rcx", "memory");
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }
}

// LOGGER is a Logger (not a pointer).
// Log lines are "<timestamp> <RDTSC|TTT> <TAG> <value>" and parsed by scripts/perf-analysis.py.
// RDTSC lines are stamped with the TSC at the end of the block, TTT lines with epoch nanos.
// The timestamp is pushed as an integer, the logger thread formats it.

// Start measuring a code block. Creates a variable called TAG in the local scope.
#define START_MEASURE(TAG) const auto TAG = Common::rdtscStart()

// End measuring a code block started with START_MEASURE(TAG), logs elapsed cycles.
// Stamped with end rather than getCurrentNanos(), the clock call costs ~15ns per measurement.
// Keep TAG at most 8 characters: " RDTSC <TAG> " then fits in 2 log elements.
#define END_MEASURE(TAG, LOGGER)                                                                \
    do {                                                                                        \
        const auto end = Common::rdtscEnd();                                                    \
        LOGGER.log("% RDTSC " #TAG " %\n", end, (end - TAG));                                  \
    } while(false)

// Time-to-tick: log the current nanosecond timestamp at a hop on the order's path.
// The difference between two hops' timestamps is the latency between them.
#define TTT_MEASURE(TAG, LOGGER)                                                                \
    do {                                                                                        \
        const auto TAG = Common::getCurrentNanos();                                             \
        LOGGER.log("% TTT " #TAG " %\n", TAG, TAG);                                         \
    } while(false)
